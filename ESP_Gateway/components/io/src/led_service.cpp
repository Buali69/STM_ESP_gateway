#include "io/led_service.h"

#include <cstdint>

#include "driver/gpio.h"
#include "esp_timer.h"

static int ledPin = -1;
static LedMode mode = LedMode::Normal;
static uint32_t last = 0;
static bool on = false;
static uint32_t newFwUntil = 0;

static uint32_t nowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void ledInit(int pin) {
    ledPin = pin;

    gpio_reset_pin(static_cast<gpio_num_t>(ledPin));
    gpio_set_direction(static_cast<gpio_num_t>(ledPin), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(ledPin), 0);
}

void ledSetMode(LedMode m) {
    mode = m;

    if (m == LedMode::NewFw) {
        newFwUntil = nowMs() + 10000;
    }
}

void ledTick() {
    if (ledPin < 0) return;

    const uint32_t now = nowMs();

    if (mode == LedMode::NewFw && now > newFwUntil) {
        mode = LedMode::Normal;
    }

    const uint32_t period =
        (mode == LedMode::Ota)   ? 100  :
        (mode == LedMode::NewFw) ? 200  :
        (mode == LedMode::Error) ? 800  :
                                  1000;

    if (now - last >= period) {
        last = now;
        on = !on;

        gpio_set_level(static_cast<gpio_num_t>(ledPin), on ? 1 : 0);
    }
}