#include "io/ota_mgr.h"

#include <cinttypes>
#include <cstdint>
#include <string>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "io/ota_client.h"

#ifndef DISABLE_LOCAL_ROLLBACK_CONFIRM_FOR_TEST
#define DISABLE_LOCAL_ROLLBACK_CONFIRM_FOR_TEST 0
#endif

namespace {
static const char* TAG = "ota_mgr";
static constexpr const char* NVS_NAMESPACE = "ota";
static constexpr const char* NVS_KEY_CONFIRMED_FW = "confirmed_fw";

static std::string s_fw;
static std::string s_bootId;

static bool s_confirmed = false;
static uint32_t s_lastConfirmTryMs = 0;
static uint32_t s_lastPollMs = 0;

static constexpr uint32_t CONFIRM_RETRY_MS = 10000;
static constexpr uint32_t OTA_POLL_MS      = 30000;

static std::string nvsReadString(const char* ns, const char* key) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return {};
    }

    size_t required = 0;
    err = nvs_get_str(handle, key, nullptr, &required);
    if (err != ESP_OK || required == 0) {
        nvs_close(handle);
        return {};
    }

    std::string out;
    out.resize(required);

    err = nvs_get_str(handle, key, out.data(), &required);
    nvs_close(handle);

    if (err != ESP_OK) {
        return {};
    }

    if (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }

    return out;
}

static bool nvsWriteString(const char* ns, const char* key, const std::string& value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(handle, key, value.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

static void tryConfirmRunningAppForRollback() {
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) {
        ESP_LOGW(TAG, "running partition is null");
        return;
    }

    esp_ota_img_states_t otaState = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t st = esp_ota_get_state_partition(running, &otaState);
    if (st != ESP_OK) {
        ESP_LOGW(TAG, "esp_ota_get_state_partition failed: %s", esp_err_to_name(st));
        return;
    }

    ESP_LOGI(TAG, "running partition state=%d", static_cast<int>(otaState));

    if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
    #if DISABLE_LOCAL_ROLLBACK_CONFIRM_FOR_TEST
        ESP_LOGW(TAG, "local rollback confirm disabled for test");
    #else
        const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "running app marked valid; rollback cancelled");
        } else {
            ESP_LOGE(TAG, "esp_ota_mark_app_valid_cancel_rollback failed: %s",
                    esp_err_to_name(err));
        }
    #endif
    }
#else
    ESP_LOGI(TAG, "rollback not enabled");
#endif
}

} // namespace

void otaMgrBegin(const std::string& fwVersion, const std::string& bootId) {
    
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        ESP_LOGI(TAG,
                "running partition label=%s subtype=%d address=0x%08" PRIx32,
                running->label,
                running->subtype,
                running->address);
    }
    
    s_fw = fwVersion;
    s_bootId = bootId;

    tryConfirmRunningAppForRollback();

    const std::string confirmedFw = nvsReadString(NVS_NAMESPACE, NVS_KEY_CONFIRMED_FW);
    s_confirmed = (confirmedFw == s_fw);

    ESP_LOGI(TAG,
             "FW version=%s confirmed_fw=%s didConfirm=%d",
             s_fw.c_str(),
             confirmedFw.c_str(),
             static_cast<int>(s_confirmed));
}

bool otaMgrIsConfirmed() {
    return s_confirmed;
}

OtaMgrEvent otaMgrPoll(bool wifiOk, bool timeOk, bool forceTick, OtaJob* outJob) {
    if (!wifiOk || !timeOk) {
        return OtaMgrEvent::None;
    }

    const uint32_t now = static_cast<uint32_t>(esp_log_timestamp());

    // 1) Confirm bis einmal erfolgreich
    if (!s_confirmed) {
        const bool due = forceTick || (now - s_lastConfirmTryMs >= CONFIRM_RETRY_MS);
        if (!due) {
            return OtaMgrEvent::None;
        }

        s_lastConfirmTryMs = now;

        const ConfirmFwResult r = confirmFwOncePerFw(s_fw, s_bootId);

        switch (r) {
            case ConfirmFwResult::Confirmed:
            case ConfirmFwResult::NoPendingJob:
                ESP_LOGI(TAG, "confirm result=ok");
                (void)nvsWriteString(NVS_NAMESPACE, NVS_KEY_CONFIRMED_FW, s_fw);
                s_confirmed = true;
                return OtaMgrEvent::ConfirmOk;

            case ConfirmFwResult::RetryableError:
            default:
                ESP_LOGW(TAG, "confirm result=RetryableError");
                return OtaMgrEvent::ConfirmTryFailed;
        }
    }

    // 2) Poll only
    const bool due = forceTick || (now - s_lastPollMs >= OTA_POLL_MS);
    if (!due) {
        return OtaMgrEvent::None;
    }

    s_lastPollMs = now;

    ESP_LOGI(TAG, "calling pollOta()");
    OtaJob job{};
    if (!pollOta(job)) {
        return OtaMgrEvent::None;
    }

    ESP_LOGI(TAG,
             "job=%" PRIu64 " file=%" PRIu64 " name=%s size=%lld url=%s",
             job.job_id,
             job.file_id,
             job.name.c_str(),
             static_cast<long long>(job.size),
             job.url.c_str());

    if (outJob) {
        *outJob = job;
    }

    return OtaMgrEvent::OtaJobReady;
}