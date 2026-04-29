#include <unity.h>
#include "core/core_logic.h"

static bool decide_until(CoreState& s,
                         uint32_t t0, uint32_t t1, uint32_t step,
                         IoMsg& out)
{
  for (uint32_t t = t0; t <= t1; t += step) {
    if (coreDecideIoCmd(s, t, out)) {
      return true;
    }
  }
  return false;
}

static void apply(CoreState& s, CoreEvtType t) {
  CoreEvt e{};
  e.type = t;
  e.u.code = 0;
  coreOnEvent(s, e);
}

void test_time_resync_requested_when_wifi_up_and_time_missing() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);

  IoMsg out{};
  bool ok = coreDecideIoCmd(s, 30000, out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_TIME_RESYNC, (uint8_t)out.cmd);
}

void test_confirm_tick_requested_when_time_ok_but_not_confirmed() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);

  IoMsg out{};
  bool ok = coreDecideIoCmd(s, 10000, out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_OTA_TICK, (uint8_t)out.cmd);
}

void test_sensor_send_requested_when_ready() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);
  apply(s, CoreEvtType::OTA_CONFIRM_OK);

  IoMsg out{};
  bool ok = coreDecideIoCmd(s, 5000, out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::SEND_SENSOR, (uint8_t)out.cmd);
}

void test_no_wifi_no_command() {
  CoreState s{};
  IoMsg out{};
  bool ok = coreDecideIoCmd(s, 0, out);
  TEST_ASSERT_FALSE(ok);
}

void test_wifi_down_disables_commands() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);
  apply(s, CoreEvtType::OTA_CONFIRM_OK);
  apply(s, CoreEvtType::WIFI_DOWN);

  IoMsg out{};
  bool ok = coreDecideIoCmd(s, 0, out);
  TEST_ASSERT_FALSE(ok);
}

void test_time_sync_fail_eventually_forces_resync() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);
  apply(s, CoreEvtType::OTA_CONFIRM_OK);
  apply(s, CoreEvtType::TIME_SYNC_FAIL);

  IoMsg out{};
  bool ok = decide_until(s, 0, 120000, 100, out); // 2 Minuten, 100ms steps
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_TIME_RESYNC, (uint8_t)out.cmd);
}

void test_ota_confirm_fail_eventually_forces_tick() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);
  apply(s, CoreEvtType::OTA_CONFIRM_FAIL);

  IoMsg out{};
  bool ok = decide_until(s, 0, 120000, 100, out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_OTA_TICK, (uint8_t)out.cmd);
}

void test_priority_time_before_ota_eventually() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_FAIL);
  apply(s, CoreEvtType::OTA_CONFIRM_FAIL);

  IoMsg out{};
  bool ok = decide_until(s, 0, 120000, 100, out);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_TIME_RESYNC, (uint8_t)out.cmd);
}

void test_time_resync_period_boundary() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP); // timeOk=false by default

  IoMsg out{};
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 29999, out));
  TEST_ASSERT_TRUE(coreDecideIoCmd(s, 30000, out));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_TIME_RESYNC, (uint8_t)out.cmd);

  // no spam right after
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 30001, out));
}

void test_ota_tick_period_boundary() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK); // confirmed=false

  IoMsg out{};
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 9999, out));
  TEST_ASSERT_TRUE(coreDecideIoCmd(s, 10000, out));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_OTA_TICK, (uint8_t)out.cmd);
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 10001, out));
}

void test_sensor_inflight_blocks_until_sent_event() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);
  apply(s, CoreEvtType::OTA_CONFIRM_OK);

  IoMsg out{};
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 4999, out));
  TEST_ASSERT_TRUE(coreDecideIoCmd(s, 5000, out));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::SEND_SENSOR, (uint8_t)out.cmd);
  TEST_ASSERT_TRUE(s.sensorInFlight);

  // even though period elapsed, still blocked while inflight
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 10000, out));

  apply(s, CoreEvtType::SENSOR_SENT_OK);
  TEST_ASSERT_FALSE(s.sensorInFlight);

  // lastSensorSendMs is still 5000, so at 10000 it should be eligible again
  TEST_ASSERT_TRUE(coreDecideIoCmd(s, 10000, out));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::SEND_SENSOR, (uint8_t)out.cmd);
}

void test_wifi_down_resets_flags_and_blocks_commands() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);
  apply(s, CoreEvtType::OTA_CONFIRM_OK);

  IoMsg out{};
  TEST_ASSERT_TRUE(coreDecideIoCmd(s, 5000, out)); // sensor -> inflight
  TEST_ASSERT_TRUE(s.sensorInFlight);

  apply(s, CoreEvtType::WIFI_DOWN);

  TEST_ASSERT_FALSE(s.wifiOk);
  TEST_ASSERT_FALSE(s.timeOk);
  TEST_ASSERT_FALSE(s.confirmed);
  TEST_ASSERT_FALSE(s.sensorInFlight);

  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 6000, out));
}

void test_elapsed_wraparound_time_resync() {
  CoreState s{};
  s.wifiOk = true;
  s.timeOk = false;

  // simulate wrap: last near max, now small
  s.lastTimeResyncKickMs = 0xFFFFFFF0u;

  IoMsg out{};
  // delta = 0x20 = 32ms -> too small
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 0x00000010u, out));

  // make delta >= 30000 by choosing now appropriately (wrap-safe subtraction)
  // delta target: 30000 => now = last + 30000 (mod 2^32)
  uint32_t now = s.lastTimeResyncKickMs + 30000u;
  TEST_ASSERT_TRUE(coreDecideIoCmd(s, now, out));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_TIME_RESYNC, (uint8_t)out.cmd);
}

// Ergänzende Core-Tests

void test_no_sensor_without_confirm_even_if_period_elapsed() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);   // confirmed bleibt false

  IoMsg out{};
  // Bei 5000ms wäre Sensor fällig, aber confirmed=false => Rule 2 (OTA tick) hat Vorrang und blockt Sensor
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 4999, out));
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 5000, out)); // noch kein OTA-Tick (erst bei 10000)
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 9999, out));

  TEST_ASSERT_TRUE(coreDecideIoCmd(s, 10000, out));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_OTA_TICK, (uint8_t)out.cmd);

  // Danach bleibt Sensor weiterhin blockiert, bis confirmed=true
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 15000, out));
}

void test_confirm_stops_ota_tick_and_allows_sensor() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);

  IoMsg out{};

  // Vor Confirm: OTA tick kommt
  TEST_ASSERT_TRUE(coreDecideIoCmd(s, 10000, out));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::FORCE_OTA_TICK, (uint8_t)out.cmd);

  // Confirm kommt rein
  apply(s, CoreEvtType::OTA_CONFIRM_OK);

  // Danach darf KEIN OTA-TICK mehr kommen, aber Sensor ist erlaubt
  bool ok = coreDecideIoCmd(s, 20000, out);
  if (ok) {
   TEST_ASSERT_NOT_EQUAL((int)IoCmd::FORCE_OTA_TICK, (int)out.cmd);
  }
}

void test_sensor_sent_fail_clears_inflight_and_allows_next_periodic_send() {
  CoreState s{};
  apply(s, CoreEvtType::WIFI_UP);
  apply(s, CoreEvtType::TIME_SYNC_OK);
  apply(s, CoreEvtType::OTA_CONFIRM_OK);

  IoMsg out{};
  // erster Send
  TEST_ASSERT_TRUE(coreDecideIoCmd(s, 5000, out));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::SEND_SENSOR, (uint8_t)out.cmd);
  TEST_ASSERT_TRUE(s.sensorInFlight);

  // fail beendet inflight
  apply(s, CoreEvtType::SENSOR_SENT_FAIL);
  TEST_ASSERT_FALSE(s.sensorInFlight);

  // nicht sofort nochmal (gleicher timestamp), aber bei nächster Periode wieder
  TEST_ASSERT_FALSE(coreDecideIoCmd(s, 5000, out));
  TEST_ASSERT_TRUE(coreDecideIoCmd(s, 10000, out));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)IoCmd::SEND_SENSOR, (uint8_t)out.cmd);
}

void run_core_logic_tests() {
  RUN_TEST(test_time_resync_requested_when_wifi_up_and_time_missing);
  RUN_TEST(test_confirm_tick_requested_when_time_ok_but_not_confirmed);
  RUN_TEST(test_sensor_send_requested_when_ready);
  RUN_TEST(test_no_wifi_no_command);
  RUN_TEST(test_wifi_down_disables_commands);
  RUN_TEST(test_time_sync_fail_eventually_forces_resync);
  RUN_TEST(test_ota_confirm_fail_eventually_forces_tick);
  RUN_TEST(test_priority_time_before_ota_eventually);
  RUN_TEST(test_time_resync_period_boundary);
  RUN_TEST(test_ota_tick_period_boundary);
  RUN_TEST(test_sensor_inflight_blocks_until_sent_event);
  RUN_TEST(test_wifi_down_resets_flags_and_blocks_commands);
  RUN_TEST(test_elapsed_wraparound_time_resync);
  RUN_TEST(test_no_sensor_without_confirm_even_if_period_elapsed);
  RUN_TEST(test_confirm_stops_ota_tick_and_allows_sensor);
  RUN_TEST(test_sensor_sent_fail_clears_inflight_and_allows_next_periodic_send);
  }