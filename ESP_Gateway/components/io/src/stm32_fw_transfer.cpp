#include <cstdio>
#include <inttypes.h>
#include <string>
#include <array>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "io/stm32_fw_transfer.h"
#include "io/stm32_uart.h"

static const char* TAG = "stm32_fw_transfer";

static bool waitForExpectedLine(const char* expected, uint32_t timeoutMs)
{
    std::string resp;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeoutTicks = pdMS_TO_TICKS(timeoutMs);

    while ((xTaskGetTickCount() - start) < timeoutTicks) {
        if (!stm32UartReadLine(resp, 200)) {
            continue;
        }

        ESP_LOGI(TAG, "RX: %s", resp.c_str());

        if (resp == expected) {
            return true;
        }

        ESP_LOGW(TAG, "Ignoring unexpected line while waiting for %s: %s",
                 expected,
                 resp.c_str());
    }

    ESP_LOGE(TAG, "Timeout waiting for %s", expected);
    return false;
}

bool stm32FwTransferBegin(uint32_t size, uint32_t crc32)
{
    char line[64];

    stm32UartSetMode(Stm32UartMode::Transfer);

    std::snprintf(
        line,
        sizeof(line),
        "OTA_PREPARE:%" PRIu32 ":%08" PRIx32,
        size,
        crc32
    );

    if (!stm32UartWriteLine(line)) {
        ESP_LOGE(TAG, "Failed to send OTA_PREPARE");
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "TX: %s", line);

    if (!waitForExpectedLine("OTA_READY", 2000)) {
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "STM ready for firmware transfer");
    return true;
}

bool stm32FwTransferDataBegin()
{
    if (!stm32UartWriteLine("OTA_DATA_BEGIN")) {
        ESP_LOGE(TAG, "Failed to send OTA_DATA_BEGIN");
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "TX: OTA_DATA_BEGIN");

    if (!waitForExpectedLine("OTA_DATA_READY", 2000)) {
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "STM ready for binary data");
    return true;
}

bool stm32FwTransferEnd()
{
    if (!stm32UartWriteLine("OTA_END")) {
        ESP_LOGE(TAG, "Failed to send OTA_END");
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "TX: OTA_END");

    if (!waitForExpectedLine("OTA_OK", 3000)) {
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "STM firmware transfer finished");
    return true;
}

static void putU32Le(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 0);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

static void putU16Le(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v >> 0);
    p[1] = static_cast<uint8_t>(v >> 8);
}

bool stm32FwTransferSendDummyChunk(uint32_t seq)
{
    constexpr uint32_t MAGIC = 0x53544D31; // "STM1"
    constexpr uint16_t LEN = 16;
    constexpr uint16_t RESERVED = 0;
    constexpr uint32_t CRC32_DUMMY = 0; // später echte CRC

    std::array<uint8_t, 16 + LEN> frame{};

    putU32Le(&frame[0], MAGIC);
    putU32Le(&frame[4], seq);
    putU16Le(&frame[8], LEN);
    putU16Le(&frame[10], RESERVED);
    putU32Le(&frame[12], CRC32_DUMMY);

    for (uint16_t i = 0; i < LEN; ++i) {
        frame[16 + i] = static_cast<uint8_t>(i);
    }

    if (!stm32UartWriteBytes(frame.data(), frame.size())) {
        ESP_LOGE(TAG, "Failed to send dummy chunk");
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "TX dummy chunk seq=%" PRIu32 " len=%u", seq, LEN);

    char expectedAck[32];
    std::snprintf(expectedAck, sizeof(expectedAck), "ACK:%" PRIu32, seq);

    if (!waitForExpectedLine(expectedAck, 2000)) {
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "Dummy chunk ACK received");
    return true;
}

static bool sendDummyChunkWithRetry(uint32_t seq, uint32_t maxRetries)
{
    for (uint32_t attempt = 1; attempt <= maxRetries; ++attempt) {
        ESP_LOGI(TAG,
                 "Sending dummy chunk seq=%" PRIu32 " attempt=%" PRIu32,
                 seq,
                 attempt);

        if (stm32FwTransferSendDummyChunk(seq)) {
            return true;
        }

        ESP_LOGW(TAG, "Retry dummy chunk seq=%" PRIu32, seq);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGE(TAG, "Dummy chunk seq=%" PRIu32 " failed permanently", seq);
    return false;
}

bool stm32FwTransferSendDummyChunks(uint32_t count)
{
    for (uint32_t seq = 0; seq < count; ++seq) {
        if (!sendDummyChunkWithRetry(seq, 3)) {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    return true;
}

bool stm32FwTransferDataEnd(uint32_t seq)
{
    constexpr uint32_t MAGIC = 0x53544D31; // "STM1"

    uint8_t frame[16] = {};

    putU32Le(&frame[0], MAGIC);
    putU32Le(&frame[4], seq);
    putU16Le(&frame[8], 0);  // len = 0 => DATA_END
    putU16Le(&frame[10], 0);
    putU32Le(&frame[12], 0);

    if (!stm32UartWriteBytes(frame, sizeof(frame))) {
        ESP_LOGE(TAG, "Failed to send DATA_END frame");
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "TX DATA_END seq=%" PRIu32, seq);

    if (!waitForExpectedLine("DATA_END_OK", 2000)) {
        ESP_LOGE(TAG, "No DATA_END_OK from STM");
        stm32UartSetMode(Stm32UartMode::Control);
        return false;
    }

    ESP_LOGI(TAG, "STM DATA phase ended");
    return true;
}