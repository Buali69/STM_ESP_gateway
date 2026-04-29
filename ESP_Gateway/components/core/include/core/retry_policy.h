#pragma once
#include <stdint.h>

struct RetryState {
  uint8_t attempt = 0;
  uint32_t lastKickMs = 0;
};

uint32_t backoffMs(uint8_t attempt, uint32_t baseMs, uint32_t capMs);

// returns true if caller should trigger the action now; updates state
bool shouldKick(RetryState& r, uint32_t nowMs, uint32_t baseMs, uint32_t capMs);

// call on success
void onSuccess(RetryState& r);