#include "core/core_logic.h"

// Tunables (können später in config)
static constexpr uint32_t TIME_RESYNC_PERIOD_MS = 30000;
static constexpr uint32_t OTA_TICK_PERIOD_MS    = 10000;
static constexpr uint32_t SENSOR_PERIOD_MS      = 5000;

void coreOnEvent(CoreState& s, const CoreEvt& e) {
  switch (e.type) {
    case CoreEvtType::WIFI_UP:
      s.wifiOk = true;
      break;

    case CoreEvtType::WIFI_DOWN:
      s.wifiOk = false;
      s.timeOk = false;
      s.confirmed = false;
      s.sensorInFlight = false;
      break;

    case CoreEvtType::TIME_SYNC_OK:
      s.timeOk = true;
      break;

    case CoreEvtType::TIME_SYNC_FAIL:
      s.timeOk = false;
      break;

    case CoreEvtType::OTA_CONFIRM_OK:
      s.confirmed = true;
      break;

    case CoreEvtType::OTA_CONFIRM_FAIL:
      // treat as not confirmed -> keep trying via decide
      s.confirmed = false;
      break;

    case CoreEvtType::OTA_RUN_OK:
      // optional
      break;

    case CoreEvtType::OTA_RUN_FAIL:
      // optional
      break;

    case CoreEvtType::SENSOR_SENT_OK:
      // send finished successfully
      s.sensorInFlight = false;
      // optional: if you want "period from success", you can set lastSensorSendMs here instead
      // s.lastSensorSendMs = e.u.nowMs;   // only if your event carries time
      break;

    case CoreEvtType::SENSOR_SENT_FAIL:
      // send finished unsuccessfully
      s.sensorInFlight = false;
      break;
  }
}

static inline bool elapsed(uint32_t now, uint32_t last, uint32_t period) {
  // works with uint32_t wraparound
  return (uint32_t)(now - last) >= period;
}

bool coreDecideIoCmd(CoreState& s, uint32_t nowMs, IoMsg& out) {
  // Rule 0: without WiFi -> no IO command
  if (!s.wifiOk) return false;

  // Rule 1 (highest prio): time missing -> periodic resync
  if (!s.timeOk) {
    if (elapsed(nowMs, s.lastTimeResyncKickMs, TIME_RESYNC_PERIOD_MS)) {
      s.lastTimeResyncKickMs = nowMs;
      out.cmd = IoCmd::FORCE_TIME_RESYNC;
      return true;
    }
    return false;
  }

  // Rule 2: time ok, but not confirmed -> periodic OTA tick
  if (!s.confirmed) {
    if (elapsed(nowMs, s.lastConfirmKickMs, OTA_TICK_PERIOD_MS)) {
      s.lastConfirmKickMs = nowMs;
      out.cmd = IoCmd::FORCE_OTA_TICK;
      return true;
    }
    return false;
  }

  // Rule 3: ready -> send sensor periodically, but never if one is in flight
  if (!s.sensorInFlight && elapsed(nowMs, s.lastSensorSendMs, SENSOR_PERIOD_MS)) {
    s.lastSensorSendMs = nowMs;
    s.sensorInFlight = true;

    out.cmd = IoCmd::SEND_SENSOR;

    // Dummy values (deterministic enough for now; real data comes from IO later if you want)
    out.u.sensor.temp = 20.0f + (nowMs % 1000) * 0.001f;
    out.u.sensor.hum  = 40.0f;

    return true;
  }

  return false;
}