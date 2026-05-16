#pragma once

#include <stdint.h>

bool stmFwVersionStore(uint32_t version);
bool stmFwVersionLoad(uint32_t& version);