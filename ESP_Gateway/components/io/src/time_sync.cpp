#include "io/time_sync.h"

#include <ctime>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/time.h"

static const char* TAG = "TIME_SYNC";

#ifndef NTP_POOL
#define NTP_POOL "pool.ntp.org"
#endif

static bool isTimePlausible() {
    const time_t now = time(nullptr);
    return now >= 1700000000;   // grober Plausibilitätswert
}

bool syncTimeOnce(uint32_t timeoutMs) {
    if (isTimePlausible()) {
        ESP_LOGI(TAG, "time already plausible: %ld", (long)time(nullptr));
        return true;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, const_cast<char*>(NTP_POOL));
    esp_sntp_init();

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeoutTicks = pdMS_TO_TICKS(timeoutMs);

    while (!isTimePlausible()) {
        if ((xTaskGetTickCount() - start) > timeoutTicks) {
            ESP_LOGW(TAG, "SNTP timeout");
            esp_sntp_stop();
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    const time_t now = time(nullptr);
    ESP_LOGI(TAG, "synced ok: %ld", (long)now);

    esp_sntp_stop();
    return true;
}