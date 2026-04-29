#include "io/sensors.h"
#include <time.h>
#include <string>
#include "io/http_client.h"
#include "esp_log.h"

static const char* TAG = "sensors";

bool sendSensorValues(float temp, float hum) {
  const uint32_t ts = (uint32_t)time(nullptr);

  char buf[128];
  snprintf(buf, sizeof(buf),
           "{\"ts\":%lu,\"values\":{\"temp\":\"%.1f\",\"hum\":\"%.1f\"}}",
           (unsigned long)ts, temp, hum);

  std::string body(buf);

  int code = 0;
  std::string resp;

  // nutzt jetzt httpsPostJson(), die intern Header+Body getrennt schreibt
  bool ok = httpsPostJson("/api/sensor/push", body, &code, &resp);
  ESP_LOGI(TAG, "sensor.push http=%d ok=%d", code, (ok && code == 200));
  return ok && (code == 200);
}
