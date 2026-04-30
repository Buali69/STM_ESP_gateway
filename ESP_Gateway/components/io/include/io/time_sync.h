#pragma once

#include <cstdint>

bool syncTimeOnce(uint32_t timeoutMs = 10000);
bool timeIsSynced();