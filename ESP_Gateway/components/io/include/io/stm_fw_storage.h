#pragma once

#include <cstdint>
#include <vector>

bool stmFwStorageInit();

bool stmFwStorageWriteKnownGood(const uint8_t* data, uint32_t size);

bool stmFwStorageReadKnownGood(std::vector<uint8_t>& out);

bool stmFwStorageHasKnownGood();

void stmFwStorageClearKnownGood();

bool stmFwStorageWriteCandidate(const uint8_t* data, uint32_t size);
bool stmFwStorageReadCandidate(std::vector<uint8_t>& out);
void stmFwStorageClearCandidate();