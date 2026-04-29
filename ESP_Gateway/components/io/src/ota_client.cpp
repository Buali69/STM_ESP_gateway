#include "io/ota_client.h"

#include <array>
#include <cinttypes>
#include <cstdint>
#include <string>

extern "C" {
#include "cJSON.h"
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "core/signing_utils.h"
#include "io/crypto_helpers.h"
#include "io/http_client.h"

namespace {
static const char* TAG = "ota_client";

static bool parseJobFromJson(const std::string& body, OtaJob& out) {
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        ESP_LOGW(TAG, "poll: invalid json");
        return false;
    }

    bool ok = false;

    do {
        cJSON* job = cJSON_GetObjectItemCaseSensitive(root, "job");
        if (!cJSON_IsObject(job)) {
            ESP_LOGI(TAG, "poll: no job object");
            break;
        }

        cJSON* jobIdItem  = cJSON_GetObjectItemCaseSensitive(job, "job_id");
        cJSON* fileIdItem = cJSON_GetObjectItemCaseSensitive(job, "file_id");
        cJSON* nameItem   = cJSON_GetObjectItemCaseSensitive(job, "name");
        cJSON* sizeItem   = cJSON_GetObjectItemCaseSensitive(job, "size");
        cJSON* shaItem    = cJSON_GetObjectItemCaseSensitive(job, "sha256");
        cJSON* urlItem    = cJSON_GetObjectItemCaseSensitive(job, "url");
        cJSON* sigItem    = cJSON_GetObjectItemCaseSensitive(job, "signature");

        if (!cJSON_IsNumber(jobIdItem) ||
            !cJSON_IsNumber(fileIdItem) ||
            !cJSON_IsString(nameItem) ||
            !cJSON_IsNumber(sizeItem) ||
            !cJSON_IsString(shaItem) ||
            !cJSON_IsString(urlItem) ||
            !cJSON_IsString(sigItem)) {
            ESP_LOGW(TAG, "poll: job fields missing/invalid");
            break;
        }

        out.job_id = static_cast<uint64_t>(jobIdItem->valuedouble);
        out.file_id = static_cast<uint64_t>(fileIdItem->valuedouble);
        out.name = nameItem->valuestring ? nameItem->valuestring : "";
        out.size = static_cast<int64_t>(sizeItem->valuedouble);
        out.sha256hex = shaItem->valuestring ? shaItem->valuestring : "";
        out.url = urlItem->valuestring ? urlItem->valuestring : "";
        out.signatureBase64 = sigItem->valuestring ? sigItem->valuestring : "";

        if (out.name.empty() ||
            out.sha256hex.empty() ||
            out.url.empty() ||
            out.signatureBase64.empty()) {
            ESP_LOGW(TAG, "poll: empty required field");
            break;
        }

        ok = true;
    } while (false);

    cJSON_Delete(root);
    return ok;
}

static bool postProgress(uint64_t jobId,
                         const char* state,
                         int progress,
                         const char* detail = nullptr) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "progress: cJSON_CreateObject failed");
        return false;
    }

    cJSON_AddNumberToObject(root, "job_id", static_cast<double>(jobId));
    cJSON_AddStringToObject(root, "state", state);
    cJSON_AddNumberToObject(root, "progress", progress);
    if (detail && *detail) {
        cJSON_AddStringToObject(root, "detail", detail);
    }

    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!raw) {
        ESP_LOGE(TAG, "progress: cJSON_PrintUnformatted failed");
        return false;
    }

    std::string json(raw);
    cJSON_free(raw);

    int code = 0;
    const bool ok = httpsPostJson("/api/ota/progress", json, &code, nullptr);

    ESP_LOGI(TAG,
             "progress job=%" PRIu64 " state=%s progress=%d http=%d",
             jobId, state, progress, code);

    return ok && code == 200;
}

static bool hexEquals(const std::array<uint8_t, 32>& dig, const std::string& hexLower) {
    if (hexLower.size() != 64) {
        return false;
    }
    return toHexLower(dig.data(), dig.size()) == hexLower;
}

static const char* otaErrorToString(OtaError e) {
    switch (e) {
        case OtaError::NoUpdatePartition:      return "no_update_partition";
        case OtaError::DownloadHttpFailed:     return "download_http_failed";
        case OtaError::DownloadTruncated:      return "download_truncated";
        case OtaError::ShaBeginFailed:         return "sha_begin_failed";
        case OtaError::ShaUpdateFailed:        return "sha_update_failed";
        case OtaError::ShaFinishFailed:        return "sha_finish_failed";
        case OtaError::Sha256Mismatch:         return "sha256_mismatch";
        case OtaError::SignatureMissing:       return "signature_missing";
        case OtaError::SignatureInvalid:       return "signature_invalid";
        case OtaError::OtaBeginFailed:         return "ota_begin_failed";
        case OtaError::OtaWriteFailed:         return "ota_write_failed";
        case OtaError::OtaEndFailed:           return "ota_end_failed";
        case OtaError::SetBootPartitionFailed: return "set_boot_partition_failed";
        case OtaError::None:
        default:
            return "unknown";
    }
}

static bool postFailed(uint64_t jobId, OtaError err, int progress = 0) {
    return postProgress(jobId, "failed", progress, otaErrorToString(err));
}

} // namespace

bool pollOta(OtaJob& out) {
    ESP_LOGI(TAG, "pollOta: start");

    int code = 0;
    std::string resp;

    const bool ok = httpsGetAuth("/api/ota/poll", &code, &resp);

    ESP_LOGI(TAG, "poll ok=%d http=%d body=%s", ok ? 1 : 0, code, resp.c_str());
    ESP_LOGI(TAG, "pollOta: body size=%u", (unsigned)resp.size());

    if (!ok || code != 200) {
        return false;
    }

    OtaJob tmp{};
    if (!parseJobFromJson(resp, tmp)) {
        return false;
    }

    out = tmp;

    ESP_LOGI(TAG,
             "pollOta: job parsed id=%" PRIu64 " name=%s size=%lld url=%s sig_len=%u",
             out.job_id,
             out.name.c_str(),
             (long long)out.size,
             out.url.c_str(),
             (unsigned)out.signatureBase64.size());

    return true;
}

ConfirmFwResult confirmFwOncePerFw(const std::string& fwVersion,
                                   const std::string& bootId) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "confirm: cJSON_CreateObject failed");
        return ConfirmFwResult::RetryableError;
    }

    cJSON_AddStringToObject(root, "fw_version", fwVersion.c_str());
    cJSON_AddStringToObject(root, "boot_id", bootId.c_str());

    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!raw) {
        ESP_LOGE(TAG, "confirm: cJSON_PrintUnformatted failed");
        return ConfirmFwResult::RetryableError;
    }

    std::string json(raw);
    cJSON_free(raw);

    int code = 0;
    std::string resp;
    const bool ok = httpsPostJson("/api/ota/confirm", json, &code, &resp);

    ESP_LOGI(TAG, "confirm try ok=%d http=%d body=%s", ok ? 1 : 0, code, resp.c_str());

    if (!ok) {
        return ConfirmFwResult::RetryableError;
    }

    if (code == 200 || code == 204) {
        return ConfirmFwResult::Confirmed;
    }

    if (code == 400 && resp.find("bad_job_id") != std::string::npos) {
        return ConfirmFwResult::NoPendingJob;
    }

    return ConfirmFwResult::RetryableError;
}

bool runOtaJob(const OtaJob& job) {
    ESP_LOGI(TAG, "runOtaJob: job_id=%" PRIu64, job.job_id);
    ESP_LOGI(TAG, "runOtaJob: url=%s", job.url.c_str());
    ESP_LOGI(TAG, "runOtaJob: sha256=%s", job.sha256hex.c_str());
    ESP_LOGI(TAG, "runOtaJob: sig_len=%u", (unsigned)job.signatureBase64.size());
    ESP_LOGI(TAG, "runOtaJob: size=%lld", (long long)job.size);

    ESP_LOGI(TAG, "runOtaJob: stack free=%u bytes",
             (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));

    ESP_LOGI(TAG,
             "runOtaJob: start job=%" PRIu64 " url=%s size=%lld sig_len=%u",
             job.job_id,
             job.url.c_str(),
             (long long)job.size,
             (unsigned)job.signatureBase64.size());

    const std::string path = urlToPath(job.url);
    ESP_LOGI(TAG, "httpsGetStream: path=%s", path.c_str());

    const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (!updatePartition) {
        postFailed(job.job_id, OtaError::NoUpdatePartition);
        ESP_LOGE(TAG, "no update partition");
        return false;
    }

    if (!postProgress(job.job_id, "downloading", 0)) {
        ESP_LOGW(TAG, "initial progress post failed");
    }

    Sha256Stream sha;
    if (!sha.begin()) {
        postFailed(job.job_id, OtaError::ShaBeginFailed);
        ESP_LOGE(TAG, "sha.begin failed");
        return false;
    }

    esp_ota_handle_t otaHandle = 0;
    esp_err_t err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
    if (err != ESP_OK) {
        postFailed(job.job_id, OtaError::OtaBeginFailed);
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return false;
    }

    size_t received = 0;
    int lastPctBucket = -1;
    bool firstChunkLogged = false;
    long long firstContentLength = -1;

    auto handler = [&](const uint8_t* data, size_t len, long long contentLength) -> bool {
        if (!firstChunkLogged) {
            firstChunkLogged = true;
            firstContentLength = contentLength;

            if (len >= 4) {
                ESP_LOGI(TAG,
                         "FW first bytes: %02X %02X %02X %02X",
                         data[0], data[1], data[2], data[3]);
            } else {
                ESP_LOGI(TAG, "FW first chunk len=%u", (unsigned)len);
            }

            ESP_LOGI(TAG, "FW contentLength=%lld", contentLength);
        }

        if (received == 0) {
            ESP_LOGI(TAG, "runOtaJob: before installing, stack free=%u bytes",
                     (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
            postProgress(job.job_id, "installing", 0);
        }

        if (!sha.update(data, len)) {
            postFailed(job.job_id, OtaError::ShaUpdateFailed);
            ESP_LOGE(TAG, "sha.update failed");
            return false;
        }

        const esp_err_t werr = esp_ota_write(otaHandle, data, len);
        if (werr != ESP_OK) {
            postFailed(job.job_id, OtaError::OtaWriteFailed);
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(werr));
            return false;
        }

        received += len;

        long long expectedLen = contentLength;
        if (expectedLen <= 0 && job.size > 0) {
            expectedLen = job.size;
        }

        if (expectedLen > 0) {
            int pct = static_cast<int>((received * 100ULL) / (uint64_t)expectedLen);
            if (pct > 100) {
                pct = 100;
            }

            int bucket = pct / 25;
            if (bucket != lastPctBucket) {
                lastPctBucket = bucket;
                postProgress(job.job_id, "installing", pct);
            }
        }

        return true;
    };

    int httpCode = 0;
    const bool httpOk = httpsGetStream(path, handler, &httpCode);

    ESP_LOGI(TAG, "httpsGetStream: http=%d firstContentLength=%lld received=%u",
             httpCode, firstContentLength, (unsigned)received);

    if (!httpOk || httpCode != 200) {
        esp_ota_abort(otaHandle);
        postFailed(job.job_id, OtaError::DownloadHttpFailed);
        ESP_LOGE(TAG, "httpsGetStream failed http=%d", httpCode);
        return false;
    }

    if (job.size > 0 && received != (size_t)job.size) {
        esp_ota_abort(otaHandle);
        postFailed(job.job_id, OtaError::DownloadTruncated, 100);
        ESP_LOGE(TAG, "truncated download received=%u expected=%lld",
                 (unsigned)received, (long long)job.size);
        return false;
    }

    ESP_LOGI(TAG, "download complete received=%u expected=%lld",
             (unsigned)received, (long long)job.size);

    std::array<uint8_t, 32> dig{};
    if (!sha.finish(dig.data())) {
        esp_ota_abort(otaHandle);
        postFailed(job.job_id, OtaError::ShaFinishFailed, 100);
        ESP_LOGE(TAG, "sha.finish failed");
        return false;
    }

    if (!hexEquals(dig, job.sha256hex)) {
        const std::string gotHex = toHexLower(dig.data(), dig.size());
        esp_ota_abort(otaHandle);
        postFailed(job.job_id, OtaError::Sha256Mismatch, 100);
        ESP_LOGE(TAG, "sha256 mismatch got=%s expected=%s",
                 gotHex.c_str(), job.sha256hex.c_str());
        return false;
    }

    if (job.signatureBase64.empty()) {
        esp_ota_abort(otaHandle);
        postFailed(job.job_id, OtaError::SignatureMissing, 100);
        ESP_LOGE(TAG, "signature missing");
        return false;
    }

    if (!verifyFirmwareSignature(dig, job.signatureBase64)) {
        esp_ota_abort(otaHandle);
        postFailed(job.job_id, OtaError::SignatureInvalid, 100);
        ESP_LOGE(TAG, "signature verification failed");
        return false;
    }

    err = esp_ota_end(otaHandle);
    if (err != ESP_OK) {
        postFailed(job.job_id, OtaError::OtaEndFailed, 100);
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK) {
        postFailed(job.job_id, OtaError::SetBootPartitionFailed, 100);
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return false;
    }

    postProgress(job.job_id, "done", 100);

    ESP_LOGI(TAG, "runOtaJob: download finished");
    ESP_LOGI(TAG, "OTA success, rebooting");

    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();

    return true;
}