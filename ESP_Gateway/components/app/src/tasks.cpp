#include "app/tasks.h"
#include "app/stm_fw_version.h"

#include <cinttypes>
#include <inttypes.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <cstring>
#include <vector>
#include <array>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "io/stm_fw_image_raw.h"

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
#include "io/stm_fw_storage.h"
#include "io/stm_fw_provider.h"
#include "io/stm32_reset.h"
#include "io/stm_fw_manifest.h"
#include "io/stm_fw_policy.h"
#include "io/crypto_helpers.h"

#include "esp_system.h"

namespace {
static volatile bool g_stmOtaDevRequested = false; //true;
static volatile bool g_stmOtaStagedRequested = false;

static bool sendStmFirmware(const uint8_t* fwData, uint32_t fwSize);

static const uint8_t* lastGoodFw = nullptr;
static size_t lastGoodFwSize = 0;

enum class StmFwState {
    Empty,
    Candidate,
    KnownGood,
    Failed
};

static StmFwState stmFwState = StmFwState::Empty;

/*
static constexpr bool ENABLE_STM_FW_TRANSFER_TEST = true;

const uint8_t* fwData = stm_fw_data;
const uint32_t fwSize = stm_fw_size;   */

static const char* TAG = "app_tasks";

static constexpr int LED_PIN = 2;
static bool tlsProbed = false;

// Queues
static QueueHandle_t ioQ  = nullptr; // Core -> IO
static QueueHandle_t evtQ = nullptr; // IO   -> Core
static QueueHandle_t otaQ = nullptr;

enum class StmOtaRequestSource : uint8_t {
    DevEmbedded,
    Server,
    Manual,
    Rollback
};

static bool runServerStmOtaJob(const OtaJob& job);

static const char* stmOtaSourceText(StmOtaRequestSource s)
{
    switch (s) {

        case StmOtaRequestSource::DevEmbedded:
            return "DevEmbedded";

        case StmOtaRequestSource::Server:
            return "Server";

        case StmOtaRequestSource::Manual:
            return "Manual";

        case StmOtaRequestSource::Rollback:
            return "Rollback";

        default:
            return "Unknown";
    }
}

struct StmOtaContext {
    StmOtaRequestSource source =
    StmOtaRequestSource::DevEmbedded;
    uint32_t fwVersion = 0;
    uint64_t jobId = 0;
    bool rollback = false;
    bool signatureRequired = false;
    bool signaturePresent = false;
};

static StmOtaContext g_stmOtaCtx{};

struct OtaTaskMsg {
    uint64_t job_id = 0;
    uint64_t file_id = 0;
    int64_t size = 0;
    char name[128] = {0};
    char sha256hex[65] = {0};
    char url[256] = {0};
    char signatureBase64[192] = {0};
    bool isStm = false;
    uint32_t stmFwVersion = 0;
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

enum class StmOtaState {
    Idle,
    Preparing,
    Transferring,
    WaitBootConfirm,
    Confirmed,
    Rollback,
    Failed
};

struct StmOtaResult {
    bool ok = false;
    bool rollbackUsed = false;
    StmOtaState finalState = StmOtaState::Idle;
    const char* error = nullptr;
};

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

void requestStagedStmOta()
{
    g_stmOtaStagedRequested = true;
}

static void copyStr(char* dst, size_t dstSize, const std::string& src) {
    if (!dst || dstSize == 0) {
        return;
    }

    const size_t n = (src.size() < (dstSize - 1)) ? src.size() : (dstSize - 1);
    memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

static const char* stmOtaStateName(StmOtaState s)
{
    switch (s) {
        case StmOtaState::Idle: return "Idle";
        case StmOtaState::Preparing: return "Preparing";
        case StmOtaState::Transferring: return "Transferring";
        case StmOtaState::WaitBootConfirm: return "WaitBootConfirm";
        case StmOtaState::Confirmed: return "Confirmed";
        case StmOtaState::Rollback: return "Rollback";
        case StmOtaState::Failed: return "Failed";
        default: return "Unknown";
    }
}

static void stmOtaSetState(StmOtaState& state, StmOtaState next)
{
    state = next;
    ESP_LOGI(TAG, "STM OTA state -> %s", stmOtaStateName(state));
}

static StmOtaResult runStmOtaUpdateManaged(const uint8_t* fwData, uint32_t fwSize)
{
    StmOtaResult result{};
    StmOtaState state = StmOtaState::Idle;

    if (!fwData || fwSize == 0) {
        result.error = "invalid firmware image";
        result.finalState = StmOtaState::Failed;
        return result;
    }

    stmOtaSetState(state, StmOtaState::Preparing);

    stm32UartSetMode(Stm32UartMode::Control);
    stm32ClearOtaReady();
    stm32UartClearBootConfirmed();

    stmReset();

    const int64_t startUs = esp_timer_get_time();
    const int64_t timeoutUs = 5000000LL;

    while (!stm32IsOtaReady() &&
           (esp_timer_get_time() - startUs) < timeoutUs) {
        stm32UartProcess();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!stm32IsOtaReady()) {
        result.error = "ota ready timeout";
        stmOtaSetState(state, StmOtaState::Failed);
        result.finalState = state;
        return result;
    }

    stmOtaSetState(state, StmOtaState::Transferring);

    if (!sendStmFirmware(fwData, fwSize)) {
        result.error = "firmware transfer failed";
        stmOtaSetState(state, StmOtaState::Failed);
        result.finalState = state;
        return result;
    }

    stmOtaSetState(state, StmOtaState::WaitBootConfirm);

    stm32UartClearBootConfirmed();

    TickType_t bootStart = xTaskGetTickCount();
    TickType_t bootTimeout = pdMS_TO_TICKS(10000);

    while ((xTaskGetTickCount() - bootStart) < bootTimeout) {
        stm32UartProcess();

        if (stm32UartIsBootConfirmed()) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (stm32UartIsBootConfirmed()) {
        stmOtaSetState(state, StmOtaState::Confirmed);

        if (stmFwStorageWriteKnownGood(fwData, fwSize)) {
            ESP_LOGI(TAG, "Stored FW as persistent known-good");
        } else {
            ESP_LOGE(TAG, "Failed to store persistent known-good");
        }

        result.ok = true;
        result.finalState = state;
        return result;
    }

    result.error = "boot confirm timeout";
    stmOtaSetState(state, StmOtaState::Rollback);

    std::vector<uint8_t> rollbackFw;

    if (!stmFwStorageReadKnownGood(rollbackFw)) {
        result.error = "boot confirm timeout and no known-good available";
        stmOtaSetState(state, StmOtaState::Failed);
        result.finalState = state;
        return result;
    }

    result.rollbackUsed = true;

    ESP_LOGW(TAG, "ROLLBACK START: sending persistent known-good");

    stm32UartSetMode(Stm32UartMode::Control);
    stm32ClearOtaReady();
    stm32UartClearBootConfirmed();

    stmReset();

    const int64_t rbStartUs = esp_timer_get_time();

    while (!stm32IsOtaReady() &&
           (esp_timer_get_time() - rbStartUs) < timeoutUs) {
        stm32UartProcess();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!stm32IsOtaReady()) {
        result.error = "rollback ota ready timeout";
        stmOtaSetState(state, StmOtaState::Failed);
        result.finalState = state;
        return result;
    }

    if (!sendStmFirmware(rollbackFw.data(), rollbackFw.size())) {
        result.error = "rollback transfer failed";
        stmOtaSetState(state, StmOtaState::Failed);
        result.finalState = state;
        return result;
    }

    stm32UartClearBootConfirmed();

    TickType_t rbBootStart = xTaskGetTickCount();

    while ((xTaskGetTickCount() - rbBootStart) < bootTimeout) {
        stm32UartProcess();

        if (stm32UartIsBootConfirmed()) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (stm32UartIsBootConfirmed()) {
        ESP_LOGW(TAG, "ROLLBACK SUCCESS: BOOT_OK received");
        stmOtaSetState(state, StmOtaState::Confirmed);
        result.ok = true;
        result.rollbackUsed = true;
        result.error = nullptr;
        result.finalState = state;
        return result;
    }

    result.error = "rollback no boot ok";
    stmOtaSetState(state, StmOtaState::Failed);
    result.finalState = state;
    return result;
}

static bool base64DecodeStd(const std::string& in, std::vector<uint8_t>& out)
{
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    out.clear();

    int val = 0;
    int valb = -8;

    for (unsigned char c : in) {
        if (c == '=') {
            break;
        }

        const char* p = strchr(tbl, c);
        if (!p) {
            return false;
        }

        val = (val << 6) + static_cast<int>(p - tbl);
        valb += 6;

        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return true;
}

static bool readDerInteger32(
    const uint8_t*& p,
    const uint8_t* end,
    uint8_t out32[32]
)
{
    if (p >= end || *p++ != 0x02) {
        return false;
    }

    if (p >= end) {
        return false;
    }

    size_t len = *p++;

    if (len == 0 || p + len > end) {
        return false;
    }

    // DER integer may contain leading 0x00 for positive sign.
    while (len > 32 && *p == 0x00) {
        ++p;
        --len;
    }

    if (len > 32) {
        return false;
    }

    memset(out32, 0, 32);
    memcpy(out32 + (32 - len), p, len);

    p += len;
    return true;
}

static bool decodeEcdsaDerSignatureToRaw64(
    const std::string& b64,
    uint8_t out[64]
)
{
    if (!out || b64.empty()) {
        return false;
    }

    std::vector<uint8_t> der;

    if (!base64DecodeStd(b64, der)) {
        ESP_LOGE(TAG, "signature base64 decode failed");
        return false;
    }

    if (der.size() < 8) {
        ESP_LOGE(TAG, "DER signature too short");
        return false;
    }

    const uint8_t* p = der.data();
    const uint8_t* end = der.data() + der.size();

    if (*p++ != 0x30) {
        ESP_LOGE(TAG, "DER signature missing sequence");
        return false;
    }

    if (p >= end) {
        return false;
    }

    size_t seqLen = *p++;

    if (seqLen & 0x80) {
        const size_t n = seqLen & 0x7F;
        if (n == 0 || n > 2 || p + n > end) {
            return false;
        }

        seqLen = 0;
        for (size_t i = 0; i < n; ++i) {
            seqLen = (seqLen << 8) | *p++;
        }
    }

    if (p + seqLen > end) {
        ESP_LOGE(TAG, "DER sequence length invalid");
        return false;
    }

    if (!readDerInteger32(p, end, out)) {
        ESP_LOGE(TAG, "DER r decode failed");
        return false;
    }

    if (!readDerInteger32(p, end, out + 32)) {
        ESP_LOGE(TAG, "DER s decode failed");
        return false;
    }

    ESP_LOGI(TAG, "DER ECDSA signature converted to raw64");
    return true;
}

static bool runServerStmOtaJob(const OtaJob& job)
{
    ESP_LOGI(TAG,
             "runServerStmOtaJob job=%llu version=%" PRIu32
             " url=%s size=%lld sha=%s",
             (unsigned long long)job.job_id,
             job.stmFwVersion,
             job.url.c_str(),
             (long long)job.size,
             job.sha256hex.c_str());

    if (job.url.empty() ||
        job.sha256hex.size() != 64 ||
        job.size <= 0 ||
        job.stmFwVersion == 0) {
        ESP_LOGE(TAG, "invalid STM server job");
        return false;
    }

    FILE* f = fopen("/stmfw/candidate.bin", "wb");
    if (!f) {
        ESP_LOGE(TAG, "failed to open STM candidate for write");
        return false;
    }

    Sha256Stream sha;
    if (!sha.begin()) {
        fclose(f);
        ESP_LOGE(TAG, "sha.begin failed");
        return false;
    }

    size_t received = 0;

    auto handler = [&](const uint8_t* data,
                       size_t len,
                       long long contentLength) -> bool {
        if (!data || len == 0) {
            return true;
        }

        if (!sha.update(data, len)) {
            ESP_LOGE(TAG, "sha.update failed");
            return false;
        }

        const size_t written = fwrite(data, 1, len, f);
        if (written != len) {
            ESP_LOGE(TAG, "candidate fwrite failed");
            return false;
        }

        received += len;

        return true;
    };

    int httpCode = 0;
    const bool httpOk = httpsGetStream(job.url, handler, &httpCode);

    fclose(f);

    if (!httpOk || httpCode != 200) {
        ESP_LOGE(TAG, "STM candidate download failed http=%d", httpCode);
        remove("/stmfw/candidate.bin");
        return false;
    }

    if (received != static_cast<size_t>(job.size)) {
        ESP_LOGE(TAG,
                 "STM candidate size mismatch received=%u expected=%lld",
                 (unsigned)received,
                 (long long)job.size);
        remove("/stmfw/candidate.bin");
        return false;
    }

    std::array<uint8_t, 32> dig{};
    if (!sha.finish(dig.data())) {
        ESP_LOGE(TAG, "sha.finish failed");
        remove("/stmfw/candidate.bin");
        return false;
    }

    char hexBuf[65] = {};

    for (size_t i = 0; i < 32; ++i) {
        snprintf(&hexBuf[i * 2],
                3,
                "%02x",
                dig[i]);
    }

    const std::string gotHex(hexBuf);

    if (gotHex != job.sha256hex) {
        ESP_LOGE(TAG,
                 "STM candidate sha mismatch got=%s expected=%s",
                 gotHex.c_str(),
                 job.sha256hex.c_str());
        remove("/stmfw/candidate.bin");
        return false;
    }

    ESP_LOGI(TAG,
             "STM candidate downloaded ok size=%u sha=%s",
             (unsigned)received,
             gotHex.c_str());

    StmFwManifest manifest{};
    manifest.magic = STM_FW_MANIFEST_MAGIC;
    manifest.manifestVersion = 1;
    manifest.fwVersion = job.stmFwVersion;
    manifest.minBootloaderVersion = 1;
    manifest.fwSize = static_cast<uint32_t>(received);

    memcpy(manifest.sha256, dig.data(), sizeof(manifest.sha256));

    manifest.flags = STM_FW_FLAG_SIGNATURE_REQUIRED;
    manifest.signatureAlg = STM_FW_SIG_ECDSA_P256_SHA256;

    // Signatur erstmal noch NICHT aus Server übernehmen,
    // weil Server aktuell DER/Base64 liefert, Manifest aber raw64 erwartet.
    //memset(manifest.signature, 0, sizeof(manifest.signature));

    if (!decodeEcdsaDerSignatureToRaw64(job.signatureBase64, manifest.signature)) {
        ESP_LOGE(TAG, "failed to decode STM signature");
        remove("/stmfw/candidate.bin");
        return false;
    }    

    g_stmOtaCtx.signatureRequired = true;
    g_stmOtaCtx.signaturePresent = true;

    if (!stmFwStorageWriteCandidateManifest(manifest)) {
        ESP_LOGE(TAG, "failed to write STM candidate manifest");
        remove("/stmfw/candidate.bin");
        return false;
    }

    g_stmOtaCtx.source = StmOtaRequestSource::Server;
    g_stmOtaCtx.jobId = job.job_id;
    g_stmOtaCtx.fwVersion = job.stmFwVersion;
    g_stmOtaCtx.rollback = false;
    g_stmOtaCtx.signatureRequired = true;
    g_stmOtaCtx.signaturePresent = false;

    requestStagedStmOta();

    ESP_LOGI(TAG, "STM server candidate staged and requested");

    return true;
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
            job.isStm = msg.isStm;
            job.stmFwVersion = msg.stmFwVersion;

            ESP_LOGI(TAG,
            "TASK isStm=%d version=%" PRIu32,
            job.isStm,
            job.stmFwVersion);

            otaMgrSetRunning(true);
            bool ok = false;

            if (job.isStm) {
                ESP_LOGI(TAG,
                        "STM OTA server job received job=%llu version=%" PRIu32
                        " url=%s size=%lld",
                        (unsigned long long)job.job_id,
                        job.stmFwVersion,
                        job.url.c_str(),
                        (long long)job.size);

                g_stmOtaCtx.source = StmOtaRequestSource::Server;
                g_stmOtaCtx.jobId = job.job_id;
                g_stmOtaCtx.fwVersion = job.stmFwVersion;
                g_stmOtaCtx.rollback = false;
                g_stmOtaCtx.signatureRequired = false;
                g_stmOtaCtx.signaturePresent = false;

                ok = runServerStmOtaJob(job);
            } else {
                ok = runOtaJob(job);
            }

            otaMgrSetRunning(false);

            otaRunning = false;
            postEvt(ok ? CoreEvtType::OTA_RUN_OK : CoreEvtType::OTA_RUN_FAIL);
        }
    }
}

void requestStmOtaDevTest()
{
    g_stmOtaDevRequested = true;
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

static bool runStmOtaUpdate(const uint8_t* fwData, uint32_t fwSize)
{
    bool ok = true;
    
   // const uint8_t* fwData = stm_fw_data;
   // const uint32_t fwSize = stm_fw_size;

    if (!fwData || fwSize == 0) {
        ESP_LOGE(TAG, "Invalid STM firmware image");
        return false;
    }

    ESP_LOGI(TAG, "STM FW transfer test size=%" PRIu32, fwSize);
    
    stm32UartSetMode(Stm32UartMode::Control);
    stm32ClearOtaReady();
    stm32UartClearBootConfirmed();

    stmReset();

    vTaskDelay(pdMS_TO_TICKS(300));

    stm32ClearOtaReady();

    ESP_LOGI(TAG, "Waiting for STM OTA_READY...");

    const int64_t startUs = esp_timer_get_time();
    const int64_t timeoutUs = 5000000LL;

    while (!stm32IsOtaReady() &&
       (esp_timer_get_time() - startUs) < timeoutUs) {

    stm32UartProcess();
    vTaskDelay(pdMS_TO_TICKS(10));
}

if (!stm32IsOtaReady()) {
        ESP_LOGE(TAG, "STM bootloader OTA_READY timeout");
        return false;
    }

    ESP_LOGI(TAG, "STM bootloader ready, starting STM OTA");

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

        if (stmFwStorageWriteKnownGood(fwData, fwSize)) {
            ESP_LOGI(TAG, "Stored FW as persistent known-good");
        } else {
            ESP_LOGE(TAG, "Failed to store persistent known-good");
        }

    } else {
        ESP_LOGW(TAG, "STM boot confirm timeout -> rollback");

        stmFwState = StmFwState::Failed;

        std::vector<uint8_t> rollbackFw;

    if (stmFwStorageReadKnownGood(rollbackFw)) {

        ESP_LOGW(TAG, "ROLLBACK START: sending persistent known-good");

        stm32UartSetMode(Stm32UartMode::Control);

        stm32ClearOtaReady();
        stm32UartClearBootConfirmed();

        stmReset();

        const int64_t rbStartUs = esp_timer_get_time();
        const int64_t rbTimeoutUs = 5000000LL;

        while (!stm32IsOtaReady() &&
            (esp_timer_get_time() - rbStartUs) < rbTimeoutUs) {

            stm32UartProcess();
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (!stm32IsOtaReady()) {

            ESP_LOGE(TAG, "ROLLBACK FAILED: STM OTA_READY timeout");

        } else {

            if (sendStmFirmware(rollbackFw.data(), rollbackFw.size())) {

                ESP_LOGI(TAG, "ROLLBACK transfer finished");

                stm32UartClearBootConfirmed();

                TickType_t rbBootStart = xTaskGetTickCount();
                TickType_t rbBootTimeout = pdMS_TO_TICKS(10000);

                while ((xTaskGetTickCount() - rbBootStart) < rbBootTimeout) {

                    stm32UartProcess();

                    if (stm32UartIsBootConfirmed()) {
                        break;
                    }

                    vTaskDelay(pdMS_TO_TICKS(50));
                }

                if (stm32UartIsBootConfirmed()) {
                    ESP_LOGW(TAG, "ROLLBACK SUCCESS: BOOT_OK received");
                } else {
                    ESP_LOGE(TAG, "ROLLBACK FAILED: no BOOT_OK");
                }

            } else {

                ESP_LOGE(TAG, "ROLLBACK FAILED: transfer failed");
            }
        }

    } else {

        ESP_LOGW(TAG, "No persistent known-good firmware available");
    }

   ok = false;
    }
    }

    stm32UartSetMode(Stm32UartMode::Control);
    ESP_LOGI(TAG, "=== STM FW TRANSFER TEST DONE ok=%d ===", ok);
    return ok;
}

static bool stageStmFirmwareCandidate(const uint8_t* fwData, uint32_t fwSize, const StmFwManifest& manifest)
{
    if (!fwData || fwSize == 0) {
        ESP_LOGE(TAG, "Invalid STM candidate data");
        return false;
    }

    if (!stmFwStorageWriteCandidate(fwData, fwSize)) {
        ESP_LOGE(TAG, "Failed to store STM candidate");
        return false;
    }

    if (!stmFwStorageWriteCandidateManifest(manifest)) {
        ESP_LOGE(TAG, "Failed to store STM candidate manifest");
        return false;
    }

    ESP_LOGI(TAG,
             "STM candidate staged: version=%" PRIu32 " size=%" PRIu32,
             manifest.fwVersion,
             manifest.fwSize);

    return true;
}

static bool runStagedStmOtaCandidate()
{
    std::vector<uint8_t> candidateFw;

    if (!stmFwStorageReadCandidate(candidateFw)) {
        ESP_LOGE(TAG, "Failed to load STM candidate");
        return false;
    }

    StmFwManifest stagedManifest{};

    if (!stmFwStorageReadCandidateManifest(stagedManifest)) {
        ESP_LOGE(TAG, "Failed to load STM candidate manifest");
        return false;
    }

    uint32_t currentVersion = 0;

    if (stmFwVersionLoad(currentVersion)) {
        ESP_LOGI(TAG, "Current STM FW version=%" PRIu32, currentVersion);
    }

    ESP_LOGI(TAG,
             "STM staged manifest: version=%" PRIu32
             " size=%" PRIu32
             " signature=%d",
             stagedManifest.fwVersion,
             stagedManifest.fwSize,
             stmFwManifestHasSignature(stagedManifest));

    StmFwPolicyResult policy = stmFwPolicyAcceptCandidate(
        stagedManifest,
        candidateFw.data(),
        static_cast<uint32_t>(candidateFw.size()),
        currentVersion
    );

    if (!policy.accepted) {
        ESP_LOGE(TAG, "STM candidate rejected: %s", policy.reason);
        return false;
    }

    ESP_LOGI(TAG, "STM candidate accepted by policy");

    StmOtaResult r = runStmOtaUpdateManaged(
        candidateFw.data(),
        static_cast<uint32_t>(candidateFw.size())
    );

    ESP_LOGI(TAG,
             "STM OTA result ok=%d rollback=%d state=%s error=%s",
             r.ok,
             r.rollbackUsed,
             stmOtaStateName(r.finalState),
             r.error ? r.error : "none");

    ESP_LOGI(TAG,
         "STM OTA AUDIT source=%s job=%llu version=%" PRIu32
         " rollback=%d sig_required=%d sig_present=%d",
         stmOtaSourceText(g_stmOtaCtx.source),
         (unsigned long long)g_stmOtaCtx.jobId,
         g_stmOtaCtx.fwVersion,
         g_stmOtaCtx.rollback,
         g_stmOtaCtx.signatureRequired,
         g_stmOtaCtx.signaturePresent);

    if (r.ok && !r.rollbackUsed) {
        stmFwStorageClearCandidate();
        stmFwStorageClearCandidateManifest();

        if (stmFwVersionStore(stagedManifest.fwVersion)) {
            ESP_LOGI(TAG,
                     "Stored STM FW version=%" PRIu32,
                     stagedManifest.fwVersion);
        }
    }

    return r.ok;
}

static bool runEmbeddedStmOtaDevTest()
{
    StmFirmwareImage img{};

    if (!stmFwGetEmbeddedTest(img)) {
        ESP_LOGE(TAG, "No embedded STM test firmware");
        return false;
    }

    ESP_LOGI(TAG,
             "STM embedded manifest: version=%" PRIu32 " size=%" PRIu32,
             img.manifest.fwVersion,
             img.manifest.fwSize);

        g_stmOtaCtx.source =
    StmOtaRequestSource::DevEmbedded;

    g_stmOtaCtx.fwVersion = img.manifest.fwVersion;
    g_stmOtaCtx.jobId = 0;
    g_stmOtaCtx.rollback = false;
    g_stmOtaCtx.signatureRequired = stmFwManifestRequiresSignature(img.manifest);
    g_stmOtaCtx.signaturePresent = stmFwManifestHasSignature(img.manifest);

    if (!stageStmFirmwareCandidate(img.data, img.size, img.manifest)) {
        return false;
    }

    requestStagedStmOta();
    return true;
   // return runStagedStmOtaCandidate();
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
    
 

    IoState st = IoState::WAIT_WIFI;
    bool printedIp = false;
    bool lastWifiOk = false;
    uint32_t lastStackLogMs = 0;
    

    for (;;) {
        ledTick();

        stm32UartProcess();

        if (g_stmOtaDevRequested) {
            g_stmOtaDevRequested = false;

            ESP_LOGI(TAG, "Manual STM OTA dev test requested");
            runEmbeddedStmOtaDevTest();
        }

        if (g_stmOtaStagedRequested) {
            g_stmOtaStagedRequested = false;

            ESP_LOGI(TAG, "Staged STM OTA requested");

            bool ok = runStagedStmOtaCandidate();

            ESP_LOGI(TAG, "Staged STM OTA finished ok=%d", ok);
        }

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
                        msg.isStm = job.isStm;
                        msg.stmFwVersion = job.stmFwVersion;
                        copyStr(msg.name, sizeof(msg.name), job.name);
                        copyStr(msg.sha256hex, sizeof(msg.sha256hex), job.sha256hex);
                        copyStr(msg.url, sizeof(msg.url), job.url);
                        copyStr(msg.signatureBase64, sizeof(msg.signatureBase64), job.signatureBase64);

                        ESP_LOGI(TAG,
                        "QUEUE isStm=%d version=%" PRIu32,
                        msg.isStm,
                        msg.stmFwVersion);

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

    if (!stm32ResetInit()) {
        ESP_LOGE(TAG, "STM reset init failed");
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