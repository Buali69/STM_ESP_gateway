#include "io/stm32_reset.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static constexpr gpio_num_t STM_NRST_PIN = GPIO_NUM_27;
static const char* TAG = "stm32_reset";

bool stm32ResetInit()
{
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << STM_NRST_PIN;
    cfg.mode = GPIO_MODE_OUTPUT_OD;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config NRST failed: %s", esp_err_to_name(err));
        return false;
    }

    gpio_set_level(STM_NRST_PIN, 1);
    return true;
}

bool stmReset()
{
    ESP_LOGI(TAG, "STM reset");

    gpio_set_level(STM_NRST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    gpio_set_level(STM_NRST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    return true;
}

bool stmRestartApp()
{
    return stmReset();
}

bool stmEnterBootloader()
{
    return stmReset();
}