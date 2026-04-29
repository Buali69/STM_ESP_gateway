#pragma once
#include <stdint.h>
#include "core/messages.h"

struct CoreState {
  bool wifiOk = false;
  bool timeOk = false;
  bool confirmed = false;

  bool sensorInFlight = false;
  uint32_t lastSensorSendMs = 0;
  uint32_t lastTimeResyncKickMs = 0;
  uint32_t lastConfirmKickMs = 0;
};

void coreOnEvent(CoreState& s, const CoreEvt& e);
bool coreDecideIoCmd(CoreState& s, uint32_t nowMs, IoMsg& out);