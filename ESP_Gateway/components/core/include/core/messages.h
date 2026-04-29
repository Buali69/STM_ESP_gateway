#pragma once
#include <stdint.h>

// ---------- Core -> IO ----------
enum class IoCmd : uint8_t {
  SET_LED_MODE,
  FORCE_TIME_RESYNC,
  FORCE_OTA_TICK,
  SEND_SENSOR
};

struct SensorPayload {
  float temp;
  float hum;
};

struct IoMsg {
  IoCmd cmd;
  union {
    uint8_t ledMode;   // LedMode als uint8_t
    SensorPayload sensor;
  } u;
};


// ---------- IO -> Core ----------
enum class CoreEvtType : uint8_t {
  WIFI_UP,
  WIFI_DOWN,
  TIME_SYNC_OK,
  TIME_SYNC_FAIL,
  OTA_CONFIRM_OK,
  OTA_CONFIRM_FAIL,
  OTA_RUN_OK,
  OTA_RUN_FAIL,
  SENSOR_SENT_OK,
  SENSOR_SENT_FAIL
};

struct CoreEvt {
  CoreEvtType type;
  union {
    int32_t code;      // optional (z.B. HTTP-Code später)
    uint32_t u32;
  } u;
};