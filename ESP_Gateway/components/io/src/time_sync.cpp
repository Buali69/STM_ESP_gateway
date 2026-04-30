#include "io/time_sync.h"

#include <ctime>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "TIME_SYNC";
static bool s_time_synced = false;
static bool s_sntp_started = false;

#ifndef NTP_POOL
#define NTP_POOL "pool.ntp.org"
#endif

// --- Plausibilitätscheck ---
static bool isTimePlausible()
{
    const time_t now = time(nullptr);
    return now >= 1700000000;   // ~2023+
}

// --- Getter ---
bool timeIsSynced()
{
    return s_time_synced;
}

// --- einmaliger Sync-Versuch ---
bool syncTimeOnce(uint32_t timeoutMs)
{
    // Wenn schon OK → fertig
    if (isTimePlausible())
    {
        s_time_synced = true;
        ESP_LOGI(TAG, "time already plausible: %ld", (long)time(nullptr));
        return true;
    }

    // SNTP nur einmal starten
    if (!s_sntp_started)
    {
        ESP_LOGI(TAG, "starting SNTP...");

        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, const_cast<char*>(NTP_POOL));
        esp_sntp_init();

        s_sntp_started = true;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeoutTicks = pdMS_TO_TICKS(timeoutMs);

    while (!isTimePlausible())
    {
        if ((xTaskGetTickCount() - start) > timeoutTicks)
        {
            ESP_LOGW(TAG, "SNTP timeout");
            s_time_synced = false;
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    const time_t now = time(nullptr);
    ESP_LOGI(TAG, "synced ok: %ld", (long)now);

    s_time_synced = true;
    return true;
}