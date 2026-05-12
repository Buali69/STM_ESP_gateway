#include "stm_fw_storage.h"

#include "esp_log.h"
#include "esp_littlefs.h"

#include <cstdio>
#include <sys/stat.h>

static const char* TAG = "stm_fw_storage";

static constexpr const char* BASE_PATH = "/stmfw";
static constexpr const char* PARTITION_LABEL = "stmfw";
static constexpr const char* KNOWN_GOOD_PATH = "/stmfw/known_good.bin";

bool stmFwStorageInit()
{
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = BASE_PATH;
    conf.partition_label = PARTITION_LABEL;
    conf.partition = nullptr;
    conf.blockdev = nullptr;
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;
    conf.read_only = false;
    conf.grow_on_mount = false;

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return false;
    }

    size_t total = 0;
    size_t used = 0;

    err = esp_littlefs_info(PARTITION_LABEL, &total, &used);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted total=%u used=%u",
                 (unsigned)total, (unsigned)used);
    }

    return true;
}

bool stmFwStorageWriteKnownGood(const uint8_t* data, size_t len)
{
    if (data == nullptr || len == 0) {
        ESP_LOGE(TAG, "invalid known-good data");
        return false;
    }

    FILE* f = fopen(KNOWN_GOOD_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "open write failed: %s", KNOWN_GOOD_PATH);
        return false;
    }

    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "write incomplete written=%u len=%u",
                 (unsigned)written, (unsigned)len);
        return false;
    }

    ESP_LOGI(TAG, "known-good stored len=%u", (unsigned)len);
    return true;
}

bool stmFwStorageReadKnownGood(std::vector<uint8_t>& out)
{
    out.clear();

    FILE* f = fopen(KNOWN_GOOD_PATH, "rb");
    if (!f) {
        ESP_LOGW(TAG, "known-good not found");
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }

    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return false;
    }

    rewind(f);

    out.resize((size_t)size);

    size_t read = fread(out.data(), 1, out.size(), f);
    fclose(f);

    if (read != out.size()) {
        ESP_LOGE(TAG, "read incomplete read=%u len=%u",
                 (unsigned)read, (unsigned)out.size());
        out.clear();
        return false;
    }

    ESP_LOGI(TAG, "known-good loaded len=%u", (unsigned)out.size());
    return true;
}

bool stmFwStorageHasKnownGood()
{
    struct stat st = {};
    return stat(KNOWN_GOOD_PATH, &st) == 0 && st.st_size > 0;
}