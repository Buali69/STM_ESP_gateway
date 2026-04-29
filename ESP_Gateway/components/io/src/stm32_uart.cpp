#include "io/stm32_uart.h"

#include <cstring>
#include <string>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

namespace {
static const char* TAG = "stm32_uart";

// UART Nummer frei wählen.
// UART0 ist meist Konsole.
// Deshalb hier UART2.
static constexpr uart_port_t UART_PORT = UART_NUM_1;

// Diese Pins an deine Verdrahtung anpassen.
static constexpr int UART_TX_PIN = GPIO_NUM_17;
static constexpr int UART_RX_PIN = GPIO_NUM_16;

// Optional, wenn nicht benutzt:
static constexpr int UART_RTS_PIN = UART_PIN_NO_CHANGE;
static constexpr int UART_CTS_PIN = UART_PIN_NO_CHANGE;

static constexpr int BAUDRATE = 115200;
static constexpr int RX_BUF_SIZE = 1024;
static constexpr int TX_BUF_SIZE = 1024;

static bool s_initialized = false;
}

bool stm32UartInit()
{
    if (s_initialized) {
        return true;
    }

    uart_config_t uart_config = {};

uart_config.baud_rate = 115200;
uart_config.data_bits = UART_DATA_8_BITS;
uart_config.parity = UART_PARITY_DISABLE;
uart_config.stop_bits = UART_STOP_BITS_1;
uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
uart_config.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_driver_install(UART_PORT, RX_BUF_SIZE, TX_BUF_SIZE, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_param_config(UART_PORT, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_RTS_PIN, UART_CTS_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return false;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "UART init ok, TX=%d RX=%d baud=%d", UART_TX_PIN, UART_RX_PIN, BAUDRATE);
    return true;
}

bool stm32UartWriteLine(const char* line)
{
    if (!s_initialized || line == nullptr) {
        return false;
    }

    const std::string msg = std::string(line) + "\n";
    const int written = uart_write_bytes(UART_PORT, msg.data(), msg.size());
    if (written < 0) {
        ESP_LOGE(TAG, "uart_write_bytes failed");
        return false;
    }

    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "TX: %s", line);
    return true;
}

bool stm32UartReadLine(std::string& out, uint32_t timeoutMs)
{
    out.clear();

    if (!s_initialized) {
        return false;
    }

    const TickType_t timeoutTicks = pdMS_TO_TICKS(timeoutMs);
    const TickType_t pollTicks = pdMS_TO_TICKS(20);

    uint8_t ch = 0;
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < timeoutTicks) {
        int len = uart_read_bytes(UART_PORT, &ch, 1, pollTicks);
        if (len > 0) {
           /* ESP_LOGI(TAG, "RX BYTE: 0x%02X '%c'",
            ch,
            (ch >= 32 && ch <= 126) ? ch : '.');
            */
            if (ch == '\n' || ch == '\r') {
                if (!out.empty()) {
                    ESP_LOGI(TAG, "RX: %s", out.c_str());
                    return true;
                }
            } else {
                if (out.size() < 255) {
                    out.push_back(static_cast<char>(ch));
                }
            }
        }
    }

    return false;
}

bool stm32Ping(uint32_t timeoutMs)
{
    if (!stm32UartWriteLine("PING")) {
        return false;
    }

    std::string reply;
    if (!stm32UartReadLine(reply, timeoutMs)) {
        ESP_LOGW(TAG, "No reply from STM32");
        return false;
    }

    if (reply == "PONG") {
        ESP_LOGI(TAG, "STM32 ping ok");
        return true;
    }

    ESP_LOGW(TAG, "Unexpected STM32 reply: %s", reply.c_str());
    return false;
}

void stm32UartProcess()
{
    std::string line;

    if (stm32UartReadLine(line, 20)) {
        ESP_LOGI(TAG, "RX STM32 LINE: %s", line.c_str());

        if (line == "PING") {
            ESP_LOGI(TAG, "TX STM32: PONG");
            stm32UartWriteLine("PONG");
        }
        else if (line == "STATUS?") {
            ESP_LOGI(TAG, "TX STM32: STATUS:OK");
            stm32UartWriteLine("STATUS:WIFI_OK:TIME_OK:OTA_IDLE");
        }
        else {
            ESP_LOGW(TAG, "Unknown STM32 command: %s", line.c_str());
            stm32UartWriteLine("ERROR:UNKNOWN_CMD");
        }
    }
}