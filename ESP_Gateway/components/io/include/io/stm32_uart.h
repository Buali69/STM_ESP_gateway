#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "esp_err.h"

bool stm32UartInit();
bool stm32UartWriteLine(const char* line);
bool stm32UartReadLine(std::string& out, uint32_t timeoutMs);
bool stm32Ping(uint32_t timeoutMs = 1000);
void stm32UartProcess();