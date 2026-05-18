#include "io/stm_fw_storage.h"

#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_crc.h"

#include <cstdio>
#include <cstring>

static const char* TAG = "stm_fw_storage";
static constexpr const char* CANDIDATE_PATH = "/stmfw/candidate.bin";

static constexpr const char* BASE_PATH = "/stmfw";
static constexpr const char* FW_PATH   = "/stmfw/known_good.bin";
static constexpr const char* META_PATH = "/stmfw/known_good.meta";

static constexpr uint32_t STM_FW_MAGIC = 0x53544D46; // "STMF"
static constexpr uint32_t STM_FW_META_VERSION = 1;

struct StmFwMeta {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t crc32;
    uint32_t valid;
};

static uint32_t calcCrc32(const uint8_t* data, uint32_t size)
{
    return esp_crc32_le(0, data, size);
}

bool stmFwStorageInit()
{
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = BASE_PATH;
    conf.partition_label = "stmfw";
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;

    esp_err_t err = esp_vfs_littlefs_register(&conf);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return false;
    }

    size_t total = 0;
    size_t used = 0;

    err = esp_littlefs_info("stmfw", &total, &used);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS stmfw mounted total=%u used=%u",
                 static_cast<unsigned>(total),
                 static_cast<unsigned>(used));
    }

    return true;
}

bool stmFwStorageWriteKnownGood(const uint8_t* data, uint32_t size)
{
    if (!data || size == 0) {
        ESP_LOGE(TAG, "Invalid known-good data");
        return false;
    }

    FILE* fw = fopen(FW_PATH, "wb");
    if (!fw) {
        ESP_LOGE(TAG, "Failed to open FW file for write");
        return false;
    }

    size_t written = fwrite(data, 1, size, fw);
    fclose(fw);

    if (written != size) {
        ESP_LOGE(TAG, "FW write incomplete: %u/%u",
                 static_cast<unsigned>(written),
                 static_cast<unsigned>(size));
        return false;
    }

    StmFwMeta meta = {};
    meta.magic = STM_FW_MAGIC;
    meta.version = STM_FW_META_VERSION;
    meta.size = size;
    meta.crc32 = calcCrc32(data, size);
    meta.valid = 1;

    FILE* mf = fopen(META_PATH, "wb");
    if (!mf) {
        ESP_LOGE(TAG, "Failed to open meta file for write");
        return false;
    }

    written = fwrite(&meta, 1, sizeof(meta), mf);
    fclose(mf);

    if (written != sizeof(meta)) {
        ESP_LOGE(TAG, "Meta write incomplete");
        return false;
    }

    ESP_LOGI(TAG, "known-good stored size=%u crc=0x%08x",
             static_cast<unsigned>(meta.size),
             static_cast<unsigned>(meta.crc32));

    return true;
}

bool stmFwStorageReadKnownGood(std::vector<uint8_t>& out)
{
    out.clear();

    FILE* mf = fopen(META_PATH, "rb");
    if (!mf) {
        ESP_LOGW(TAG, "No known-good meta");
        return false;
    }

    StmFwMeta meta = {};
    size_t read = fread(&meta, 1, sizeof(meta), mf);
    fclose(mf);

    if (read != sizeof(meta)) {
        ESP_LOGE(TAG, "Meta read incomplete");
        return false;
    }

    if (meta.magic != STM_FW_MAGIC ||
        meta.version != STM_FW_META_VERSION ||
        meta.valid != 1 ||
        meta.size == 0) {
        ESP_LOGW(TAG, "Invalid known-good meta");
        return false;
    }

    FILE* fw = fopen(FW_PATH, "rb");
    if (!fw) {
        ESP_LOGE(TAG, "No known-good FW file");
        return false;
    }

    out.resize(meta.size);

    read = fread(out.data(), 1, meta.size, fw);
    fclose(fw);

    if (read != meta.size) {
        ESP_LOGE(TAG, "FW read incomplete: %u/%u",
                 static_cast<unsigned>(read),
                 static_cast<unsigned>(meta.size));
        out.clear();
        return false;
    }

    uint32_t crc = calcCrc32(out.data(), meta.size);

    if (crc != meta.crc32) {
        ESP_LOGE(TAG, "known-good CRC mismatch stored=0x%08x calc=0x%08x",
                 static_cast<unsigned>(meta.crc32),
                 static_cast<unsigned>(crc));
        out.clear();
        return false;
    }

    ESP_LOGI(TAG, "known-good loaded size=%u crc=0x%08x",
             static_cast<unsigned>(meta.size),
             static_cast<unsigned>(meta.crc32));

    return true;
}

bool stmFwStorageHasKnownGood()
{
    std::vector<uint8_t> tmp;
    return stmFwStorageReadKnownGood(tmp);
}

void stmFwStorageClearKnownGood()
{
    remove(FW_PATH);
    remove(META_PATH);
    ESP_LOGW(TAG, "known-good cleared");
}

bool stmFwStorageWriteCandidate(const uint8_t* data, uint32_t size)
{
    if (!data || size == 0) {
        ESP_LOGE(TAG, "Invalid candidate data");
        return false;
    }

    FILE* fw = fopen(CANDIDATE_PATH, "wb");
    if (!fw) {
        ESP_LOGE(TAG, "Failed to open candidate file for write");
        return false;
    }

    size_t written = fwrite(data, 1, size, fw);
    fclose(fw);

    if (written != size) {
        ESP_LOGE(TAG, "Candidate write incomplete: %u/%u",
                 static_cast<unsigned>(written),
                 static_cast<unsigned>(size));
        return false;
    }

    ESP_LOGI(TAG, "candidate stored size=%u", static_cast<unsigned>(size));
    return true;
}

bool stmFwStorageReadCandidate(std::vector<uint8_t>& out)
{
    out.clear();

    FILE* fw = fopen(CANDIDATE_PATH, "rb");
    if (!fw) {
        ESP_LOGW(TAG, "No candidate FW file");
        return false;
    }

    fseek(fw, 0, SEEK_END);
    long size = ftell(fw);
    rewind(fw);

    if (size <= 0) {
        fclose(fw);
        ESP_LOGE(TAG, "Candidate size invalid");
        return false;
    }

    out.resize(static_cast<size_t>(size));

    size_t read = fread(out.data(), 1, out.size(), fw);
    fclose(fw);

    if (read != out.size()) {
        ESP_LOGE(TAG, "Candidate read incomplete");
        out.clear();
        return false;
    }

    ESP_LOGI(TAG, "candidate loaded size=%u", static_cast<unsigned>(out.size()));
    return true;
}

void stmFwStorageClearCandidate()
{
    remove(CANDIDATE_PATH);
    ESP_LOGI(TAG, "candidate cleared");
}