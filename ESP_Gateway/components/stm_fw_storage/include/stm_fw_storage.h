#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

bool stmFwStorageInit();

bool stmFwStorageWriteKnownGood(const uint8_t* data, size_t len);
bool stmFwStorageReadKnownGood(std::vector<uint8_t>& out);
bool stmFwStorageHasKnownGood();