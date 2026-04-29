#include "core/retry_policy.h"

uint32_t backoffMs(uint8_t attempt, uint32_t baseMs, uint32_t capMs) {
  if (attempt == 0) attempt = 1;

  // base * 2^(attempt-1), capped
  uint32_t ms = baseMs;
  for (uint8_t i = 1; i < attempt; ++i) {
    if (ms > capMs / 2) { ms = capMs; break; }
    ms *= 2;
  }
  if (ms > capMs) ms = capMs;
  return ms;
}

bool shouldKick(RetryState& r, uint32_t nowMs, uint32_t baseMs, uint32_t capMs) {
  const uint8_t nextAttempt = (r.attempt == 255) ? 255 : (uint8_t)(r.attempt + 1);
  const uint32_t waitMs = backoffMs(nextAttempt, baseMs, capMs);

  if ((uint32_t)(nowMs - r.lastKickMs) < waitMs) return false;

  r.attempt = nextAttempt;
  r.lastKickMs = nowMs;
  return true;
}

void onSuccess(RetryState& r) {
  r.attempt = 0;
  r.lastKickMs = 0;
}