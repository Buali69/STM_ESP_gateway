#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "esp_err.h"

enum class Stm32UartMode {
    Control,
    Transfer
};

void stm32UartSetMode(Stm32UartMode mode);
Stm32UartMode stm32UartGetMode();
bool stm32UartIsTransferMode();

bool stm32UartInit();
bool stm32UartWriteLine(const char* line);
bool stm32UartReadLine(std::string& out, uint32_t timeoutMs);
bool stm32Ping(uint32_t timeoutMs = 1000);
void stm32UartProcess();
bool stm32UartWriteBytes(const uint8_t* data, size_t len);

void stm32UartClearBootConfirmed(void);
bool stm32UartIsBootConfirmed(void);

void stm32ClearOtaReady();
bool stm32IsOtaReady();