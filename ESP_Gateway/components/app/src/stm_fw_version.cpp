#include "app/stm_fw_version.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char* TAG = "stm_fw_version";

static constexpr const char* NVS_NS  = "stm_fw";
static constexpr const char* KEY_VER = "version";

bool stmFwVersionStore(uint32_t version)
{
    nvs_handle_t h;

    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u32(h, KEY_VER, version);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "store version failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "stored STM FW version=%" PRIu32, version);
    return true;
}

bool stmFwVersionLoad(uint32_t& version)
{
    version = 0;

    nvs_handle_t h;

    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_get_u32(h, KEY_VER, &version);
    nvs_close(h);

    return err == ESP_OK;
}