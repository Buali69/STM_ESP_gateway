#include "app/tasks.h"

#include <cinttypes>
#include <inttypes.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <cstring>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "app/stm_fw_image_raw.h"

#include "core/messages.h"
#include "core/core_logic.h"
#include "core/retry_policy.h"

#include "io/config.h"
#include "io/time_sync.h"
#include "io/wifi_mgr.h"
#include "io/led_service.h"
#include "io/ota_mgr.h"
#include "io/sensors.h"
#include "io/http_client.h"
#include "io/sensors.h"
#include "io/stm32_uart.h"
#include "io/stm32_fw_transfer.h"
#include "stm_fw_storage.h"

namespace {
static const uint8_t* lastGoodFw = nullptr;
static size_t lastGoodFwSize = 0;

enum class StmFwState {
    Empty,
    Candidate,
    KnownGood,
    Failed
};

static StmFwState stmFwState = StmFwState::Empty;

static constexpr bool ENABLE_STM_FW_TRANSFER_TEST = true;

const uint8_t* fwData = stm_fw_data;
const uint32_t fwSize = stm_fw_size;

static const char* TAG = "app_tasks";

static constexpr int LED_PIN = 2;
static bool tlsProbed = false;

// Queues
static QueueHandle_t ioQ  = nullptr; // Core -> IO
static QueueHandle_t evtQ = nullptr; // IO   -> Core
static QueueHandle_t otaQ = nullptr;

struct OtaTaskMsg {
    uint64_t job_id = 0;
    uint64_t file_id = 0;
    int64_t size = 0;
    char name[128] = {0};
    char sha256hex[65] = {0};
    char url[256] = {0};
    char signatureBase64[192] = {0};
};

static bool otaRunning = false;

enum class IoState : uint8_t {
    WAIT_WIFI,
    WAIT_TIME,
    CONFIRM_FW,
    NORMAL,
    ERROR
};

static bool timeSynced = false;
static bool forceOtaTick = false;
static std::string bootId;

static uint32_t nowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

// ---- helper: send event to core (non-blocking) ----
static inline void postEvt(CoreEvtType t, int32_t code = 0) {
    if (!evtQ) return;

    CoreEvt e{};
    e.type = t;
    e.u.code = code;
    (void)xQueueSend(evtQ, &e, 0);
}

struct SensorRetry {
    bool pending = false;
    SensorPayload last{};
    uint8_t attempt = 0;
    uint32_t nextTryMs = 0;
};

static SensorRetry sRetry;

static void copyStr(char* dst, size_t dstSize, const std::string& src) {
    if (!dst || dstSize == 0) {
        return;
    }

    const size_t n = (src.size() < (dstSize - 1)) ? src.size() : (dstSize - 1);
    memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

// Tuning
static constexpr uint8_t  SENSOR_MAX_ATTEMPTS   = 5;
static constexpr uint32_t SENSOR_BACKOFF_BASE_MS = 2000;
static constexpr uint32_t SENSOR_BACKOFF_MAX_MS  = 60000;

//------------------ OTA Task --------------

static void otaTask(void*) {
    for (;;) {
        OtaTaskMsg msg{};

        if (otaQ && xQueueReceive(otaQ, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "OTA task start job=%" PRIu64, msg.job_id);
            ESP_LOGI(TAG, "TASK job url=%s", msg.url);

            OtaJob job{};
            job.job_id = msg.job_id;
            job.file_id = msg.file_id;
            job.size = msg.size;
            job.name = msg.name;
            job.sha256hex = msg.sha256hex;
            job.url = msg.url;
            job.signatureBase64 = msg.signatureBase64;

            otaMgrSetRunning(true);
            const bool ok = runOtaJob(job);
            otaMgrSetRunning(false);

            otaRunning = false;
            postEvt(ok ? CoreEvtType::OTA_RUN_OK : CoreEvtType::OTA_RUN_FAIL);
        }
    }
}


static bool sendStmFirmware(const uint8_t* fwData, uint32_t fwSize)
{
    bool ok = true;

    const uint32_t fwCrc = stm32FwTransferCalcCrc32(fwData, fwSize);

    stmFwState = StmFwState::Candidate;
    ESP_LOGI(TAG, "STM FW state -> Candidate");

    if (!stm32FwTransferBegin(fwSize, fwCrc)) {
        ESP_LOGE(TAG, "OTA_PREPARE failed");
        ok = false;
    }

    if (ok && !stm32FwTransferDataBegin()) {
        ESP_LOGE(TAG, "OTA_DATA_BEGIN failed");
        ok = false;
    }

    if (ok) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (ok && !stm32FwTransferSendBuffer(fwData, fwSize)) {
        ESP_LOGE(TAG, "Firmware transfer failed");
        ok = false;
    }

    const uint32_t dataEndSeq = (fwSize + 255u) / 256u;

    if (ok && !stm32FwTransferDataEnd(dataEndSeq)) {
        ESP_LOGE(TAG, "DATA_END failed");
        ok = false;
    }

    if (ok && !stm32FwTransferEnd()) {
        ESP_LOGE(TAG, "OTA_END failed");
        ok = false;
    }

    if (!ok) {
        stm32FwTransferAbort();
    }

    return ok;
}

static bool runStmOtaTest()
{
    bool ok = true;
    
    const uint8_t* fwData = stm_fw_data;
    const uint32_t fwSize = stm_fw_size;

    ESP_LOGI(TAG, "STM FW transfer test size=%" PRIu32, fwSize);
    
    if (!sendStmFirmware(fwData, fwSize)) {
        return false;
    }

    if (ok) {
       ESP_LOGI(TAG, "WAIT_BOOT_CONFIRM");

       stm32UartClearBootConfirmed();

       TickType_t start = xTaskGetTickCount();
       TickType_t timeout = pdMS_TO_TICKS(10000);

        while ((xTaskGetTickCount() - start) < timeout) {
            stm32UartProcess();

             if (stm32UartIsBootConfirmed()) {
                  break;
             }

            vTaskDelay(pdMS_TO_TICKS(50));
        }

        if (stm32UartIsBootConfirmed()) {
            ESP_LOGI(TAG, "STM update confirmed -> known good");

            stmFwState = StmFwState::KnownGood;

            lastGoodFw = fwData;
            lastGoodFwSize = fwSize;

            stmFwStorageWriteKnownGood(fwData, fwSize);
            ESP_LOGI(TAG, "Stored FW as known-good");
        } else {
            ESP_LOGW(TAG, "STM boot confirm timeout -> rollback");

            stmFwState = StmFwState::Failed;

            std::vector<uint8_t> rollbackFw;

        if (stmFwStorageReadKnownGood(rollbackFw)) {
            ESP_LOGI(TAG, "Rollback: sending known-good from flash");
            sendStmFirmware(rollbackFw.data(), rollbackFw.size());
        } else {
            ESP_LOGW(TAG, "No known-good firmware available");
        }

            ok = false;
        }
    }

    stm32UartSetMode(Stm32UartMode::Control);
    ESP_LOGI(TAG, "=== STM FW TRANSFER TEST DONE ok=%d ===", ok);
    return ok;
}

// ---------------- IO TASK ----------------
static void ioTask(void*) {
    ledInit(LED_PIN);
    ledSetMode(LedMode::Normal);

    char bootBuf[17];
    std::snprintf(bootBuf, sizeof(bootBuf), "%08" PRIx32 "%08" PRIx32, esp_random(), esp_random());
    bootId = bootBuf;

    ESP_LOGI(TAG, "BOOT id=%s", bootId.c_str());

    otaMgrBegin(std::string(FW_VERSION), bootId);
    wifiInit();

    if (!stm32UartInit()) {
        ESP_LOGE(TAG, "STM32 UART init failed");
    } 
    else {
        ESP_LOGI(TAG, "STM32 UART ready");
    }
    
    static bool testSent = false;

    if (!testSent) {
        testSent = true;
        stm32ClearOtaReady();

        ESP_LOGI(TAG, "Waiting for STM OTA_READY...");

        const int64_t start = esp_timer_get_time();

        while (!stm32IsOtaReady() &&
            (esp_timer_get_time() - start) < 5000000LL) {

            stm32UartProcess();
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        ESP_LOGI(TAG, "Wait finished, otaReady=%d", stm32IsOtaReady());

        if (!stm32IsOtaReady()) {
            ESP_LOGE(TAG, "STM bootloader OTA_READY timeout");
        } else {
            ESP_LOGI(TAG, "STM bootloader ready, starting STM OTA");
            runStmOtaTest();
        }
    }

    IoState st = IoState::WAIT_WIFI;
    bool printedIp = false;
    bool lastWifiOk = false;
    uint32_t lastStackLogMs = 0;
    

    for (;;) {
        ledTick();

        stm32UartProcess();

        const uint32_t now = nowMs();

        if (now - lastStackLogMs > 10000) {
            lastStackLogMs = now;
            UBaseType_t w = uxTaskGetStackHighWaterMark(nullptr);
            ESP_LOGI(TAG, "STACK io free=%u bytes", static_cast<unsigned>(w * sizeof(StackType_t)));
        }

        // ---- drain Core->IO commands (non-blocking) ----
        IoMsg m{};
        while (ioQ && xQueueReceive(ioQ, &m, 0) == pdTRUE) {
            switch (m.cmd) {
                case IoCmd::SET_LED_MODE:
                    ledSetMode(static_cast<LedMode>(m.u.ledMode));
                    break;

                case IoCmd::FORCE_TIME_RESYNC:
                    timeSynced = false;
                    break;

                case IoCmd::FORCE_OTA_TICK:
                    forceOtaTick = true;
                    break;

                case IoCmd::SEND_SENSOR:
                    sRetry.last = m.u.sensor;
                    sRetry.pending = true;
                    sRetry.attempt = 0;
                    sRetry.nextTryMs = now;
                    ESP_LOGI(TAG,
                             "SENSOR queued temp=%.2f hum=%.2f",
                             sRetry.last.temp,
                             sRetry.last.hum);
                    break;

                default:
                    break;
            }
        }

        const bool wifiOk = wifiIsConnected();

        // WiFi edge events
        if (wifiOk != lastWifiOk) {
            postEvt(wifiOk ? CoreEvtType::WIFI_UP : CoreEvtType::WIFI_DOWN);
            lastWifiOk = wifiOk;
        }

        switch (st) {
            case IoState::WAIT_WIFI:
                if (wifiOk) {
                    if (!printedIp) {
                        ESP_LOGI(TAG, "WiFi OK, IP=%s", wifiLocalIpStr().c_str());
                        printedIp = true;
                    }
                    st = IoState::WAIT_TIME;
                } else {
                    printedIp = false;
                    ledSetMode(LedMode::Error);
                }
                break;

            case IoState::WAIT_TIME:
                if (!wifiOk) {
                    timeSynced = false;
                    st = IoState::WAIT_WIFI;
                    break;
                }

                if (!timeSynced) {
                    timeSynced = syncTimeOnce();
                    ESP_LOGI(TAG,
                             "TIME epoch=%lu synced=%d",
                             static_cast<unsigned long>(time(nullptr)),
                             static_cast<int>(timeSynced));

                    postEvt(timeSynced ? CoreEvtType::TIME_SYNC_OK : CoreEvtType::TIME_SYNC_FAIL);
                }

                if (timeSynced && !tlsProbed) {
                    tlsProbed = true;
                    tlsDiagnostics();
                }

                if (timeSynced) {
                    if (otaMgrIsConfirmed()) {
                        ESP_LOGI(TAG, "OTA already confirmed at boot");
                        postEvt(CoreEvtType::OTA_CONFIRM_OK);
                        st = IoState::NORMAL;
                    } else {
                        st = IoState::CONFIRM_FW;
                    }
                } else {
                    ledSetMode(LedMode::Error);
                }
                break;

            case IoState::CONFIRM_FW: {
                if (!wifiOk || !timeSynced) {
                    st = IoState::WAIT_WIFI;
                    break;
                }

                OtaJob job{};
                const OtaMgrEvent ev = otaMgrPoll(wifiOk, timeSynced, forceOtaTick, &job);
                forceOtaTick = false;

                if (ev == OtaMgrEvent::ConfirmOk) {
                    postEvt(CoreEvtType::OTA_CONFIRM_OK);
                    ledSetMode(LedMode::NewFw);
                    st = IoState::NORMAL;
                } else if (ev == OtaMgrEvent::ConfirmTryFailed) {
                    postEvt(CoreEvtType::OTA_CONFIRM_FAIL);
                    ledSetMode(LedMode::Error);
                }
                break;
            }

           case IoState::NORMAL: {
                if (!wifiOk || !timeSynced) {
                    ledSetMode(LedMode::Error);
                    st = IoState::WAIT_WIFI;
                    break;
                }

                if (!otaMgrIsConfirmed()) {
                    st = IoState::CONFIRM_FW;
                    break;
                }

                ledSetMode(LedMode::Normal);

                if (!otaRunning) {
                    OtaJob job{};
                    const OtaMgrEvent ev = otaMgrPoll(wifiOk, timeSynced, forceOtaTick, &job);
                    forceOtaTick = false;

                    if (ev == OtaMgrEvent::OtaJobReady) {
                        OtaTaskMsg msg{};
                        msg.job_id = job.job_id;
                        msg.file_id = job.file_id;
                        msg.size = job.size;
                        copyStr(msg.name, sizeof(msg.name), job.name);
                        copyStr(msg.sha256hex, sizeof(msg.sha256hex), job.sha256hex);
                        copyStr(msg.url, sizeof(msg.url), job.url);
                        copyStr(msg.signatureBase64, sizeof(msg.signatureBase64), job.signatureBase64);

                        ESP_LOGI(TAG, "QUEUE job url=%s", msg.url);

                        if (otaQ && xQueueSend(otaQ, &msg, 0) == pdTRUE) {
                            otaRunning = true;
                            ESP_LOGI(TAG,
                                    "OTA queued job=%" PRIu64,
                                    job.job_id);
                        } else {
                            ESP_LOGW(TAG, "OTA queue full");
                            postEvt(CoreEvtType::OTA_RUN_FAIL);
                            ledSetMode(LedMode::Error);
                        }
                    }
                } else {
                    forceOtaTick = false;
                }

                break;
            }

            case IoState::ERROR:
            default:
                ledSetMode(LedMode::Error);
                if (wifiOk) {
                    st = IoState::WAIT_TIME;
                }
                break;
        }

        // Sensor retry/attempt
        if (sRetry.pending && wifiOk && timeSynced) {
            ESP_LOGI(TAG,
                     "SENSOR gate pending=%d wifi=%d time=%d confirmed=%d",
                     static_cast<int>(sRetry.pending),
                     static_cast<int>(wifiOk),
                     static_cast<int>(timeSynced),
                     static_cast<int>(otaMgrIsConfirmed()));

            if (sRetry.nextTryMs == 0 || static_cast<int32_t>(now - sRetry.nextTryMs) >= 0) {
                sRetry.attempt++;

                ESP_LOGI(TAG,
                         "SENSOR try=%u temp=%.2f hum=%.2f",
                         sRetry.attempt,
                         sRetry.last.temp,
                         sRetry.last.hum);

                const bool ok = sendSensorValues(sRetry.last.temp, sRetry.last.hum);

                ESP_LOGI(TAG, "SENSOR send result=%s", ok ? "OK" : "FAIL");

                if (ok) {
                    sRetry.pending = false;
                    sRetry.attempt = 0;
                    sRetry.nextTryMs = 0;
                    ESP_LOGI(TAG, "SENSOR postEvt SENSOR_SENT_OK");
                    postEvt(CoreEvtType::SENSOR_SENT_OK);
                } else {
                    ESP_LOGW(TAG, "SENSOR postEvt SENSOR_SENT_FAIL");
                    postEvt(CoreEvtType::SENSOR_SENT_FAIL);

                    if (sRetry.attempt >= SENSOR_MAX_ATTEMPTS) {
                        ESP_LOGW(TAG, "SENSOR giving up");
                        sRetry.pending = false;
                        sRetry.nextTryMs = 0;
                    } else {
                        const uint32_t d = backoffMs(
                            sRetry.attempt,
                            SENSOR_BACKOFF_BASE_MS,
                            SENSOR_BACKOFF_MAX_MS);

                        sRetry.nextTryMs = now + d;
                        ESP_LOGI(TAG, "SENSOR retry in %lu ms", static_cast<unsigned long>(d));
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ---------------- CORE TASK ----------------
static void coreTask(void*) {
    TickType_t last = xTaskGetTickCount();
    CoreState state;

    for (;;) {
        CoreEvt e{};
        while (evtQ && xQueueReceive(evtQ, &e, 0) == pdTRUE) {
            coreOnEvent(state, e);

            switch (e.type) {
                case CoreEvtType::WIFI_UP:
                    ESP_LOGI(TAG, "CORE WIFI_UP");
                    break;
                case CoreEvtType::WIFI_DOWN:
                    ESP_LOGW(TAG, "CORE WIFI_DOWN");
                    break;
                case CoreEvtType::TIME_SYNC_OK:
                    ESP_LOGI(TAG, "CORE TIME_SYNC_OK");
                    break;
                case CoreEvtType::TIME_SYNC_FAIL:
                    ESP_LOGW(TAG, "CORE TIME_SYNC_FAIL");
                    break;
                case CoreEvtType::OTA_CONFIRM_OK:
                    ESP_LOGI(TAG, "CORE OTA_CONFIRM_OK");
                    break;
                case CoreEvtType::OTA_CONFIRM_FAIL:
                    ESP_LOGW(TAG, "CORE OTA_CONFIRM_FAIL");
                    break;
                case CoreEvtType::OTA_RUN_OK:
                    ESP_LOGI(TAG, "CORE OTA_RUN_OK");
                    break;
                case CoreEvtType::OTA_RUN_FAIL:
                    ESP_LOGW(TAG, "CORE OTA_RUN_FAIL");
                    break;
                case CoreEvtType::SENSOR_SENT_OK:
                    ESP_LOGI(TAG, "CORE SENSOR_SENT_OK");
                    break;
                case CoreEvtType::SENSOR_SENT_FAIL:
                    ESP_LOGW(TAG, "CORE SENSOR_SENT_FAIL");
                    break;
                default:
                    break;
            }
        }

        IoMsg out{};
        const uint32_t now = nowMs();
        if (coreDecideIoCmd(state, now, out)) {
            (void)xQueueSend(ioQ, &out, 0);
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    }
}

} // namespace

void appTasksStart() {
    ESP_LOGI(TAG, "appTasksStart");

    if (!stmFwStorageInit()) {
        ESP_LOGW(TAG, "STM FW storage init failed");
    }

    ioQ  = xQueueCreate(8, sizeof(IoMsg));
    evtQ = xQueueCreate(16, sizeof(CoreEvt));
    otaQ = xQueueCreate(2, sizeof(OtaTaskMsg));

    configASSERT(ioQ != nullptr);
    configASSERT(evtQ != nullptr);
    configASSERT(otaQ != nullptr);

   // xTaskCreatePinnedToCore(coreTask, "core", 4096,  nullptr, 2, nullptr, 0);
   // xTaskCreatePinnedToCore(ioTask,   "io",   16384, nullptr, 1, nullptr, 0);
   // xTaskCreatePinnedToCore(otaTask,  "ota",  12288, nullptr, 1, nullptr, 0);

    xTaskCreate(coreTask, "coreTask", 4096, nullptr, 5, nullptr);
    xTaskCreate(ioTask,   "ioTask",   16384, nullptr, 5, nullptr);
    xTaskCreate(otaTask,  "otaTask",  12288, nullptr, 5, nullptr);
}