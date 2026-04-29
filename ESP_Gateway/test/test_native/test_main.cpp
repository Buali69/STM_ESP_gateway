#include <unity.h>

/*
TEST MATRIX — Core Logic + Retry Policy
======================================

Retry Policy (core/retry_policy.h/.cpp)
--------------------------------------
R1  backoff: attempt==0 treated as 1
    - test_backoff_attempt0_is_1

R2  backoff: exponential growth (base * 2^(attempt-1))
    - test_backoff_basic

R3  backoff: capped to capMs
    - test_backoff_caps

R4  shouldKick: first kick only after baseMs from lastKickMs (default lastKickMs=0)
    - test_shouldKick_basic

R5  shouldKick: updates state on kick (attempt++, lastKickMs=now) + next wait doubles
    - test_shouldKick_basic (second kick assertions)

R6  onSuccess: resets retry state (attempt -> 0; (optional) lastKickMs policy)
    - test_onSuccess_resets


Core State Machine (core/core_logic.h/.cpp)
------------------------------------------
C0  No WiFi -> no IO command
    - test_no_wifi_no_command

C1  WIFI_DOWN resets: wifiOk=false, timeOk=false, confirmed=false, sensorInFlight=false
    AND blocks commands
    - test_wifi_down_resets_flags_and_blocks_commands
    - test_wifi_down_disables_commands

C2  Priority: time missing has highest priority (timeOk=false -> periodic resync)
    - test_time_resync_requested_when_wifi_up_and_time_missing
    - test_time_resync_period_boundary
    - test_time_sync_fail_eventually_forces_resync
    - test_priority_time_before_ota_eventually
    - test_elapsed_wraparound_time_resync

C3  Time OK but not confirmed -> periodic OTA tick
    - test_confirm_tick_requested_when_time_ok_but_not_confirmed
    - test_ota_tick_period_boundary
    - test_ota_confirm_fail_eventually_forces_tick

C4  Confirm OK transitions to "ready for sensor"
    - test_sensor_send_requested_when_ready
    - test_confirm_transitions_to_sensor_path (oder: test_confirm_stops_ota_tick_and_allows_sensor)

C5  Sensor: periodic send when ready; sets sensorInFlight=true and blocks further sends while in flight
    - test_sensor_inflight_blocks_until_sent_event

C6  Sensor completion events clear inFlight (OK and FAIL)
    - test_sensor_inflight_blocks_until_sent_event (OK)
    - test_sensor_sent_fail_clears_inflight_and_allows_next_periodic_send (FAIL)

C7  No sensor sends while not confirmed (Rule2 blocks Rule3 even if sensor period elapsed)
    - test_no_sensor_without_confirm_even_if_period_elapsed

C8  Wraparound safety for elapsed() in time-resync path
    - test_elapsed_wraparound_time_resync


Coverage notes / intentional gaps
--------------------------------
G1  OTA_RUN_OK / OTA_RUN_FAIL are currently "optional" (no behavior) -> no tests.
G2  Dummy sensor payload values are deterministic but not part of spec -> not asserted.
G3  lastSensorSendMs updates on decide (not on SENT_OK/FAIL); tests assume this behavior.
*/

extern void run_retry_policy_tests();
extern void run_core_logic_tests();
//extern void run_signing_utils_tests();

int main(int argc, char **argv) {
  UNITY_BEGIN();

  run_retry_policy_tests();
  run_core_logic_tests();
  //run_signing_utils_tests();
  

  return UNITY_END();
}