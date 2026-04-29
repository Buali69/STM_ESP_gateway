#include <unity.h>
#include "core/retry_policy.h"


void test_backoff_basic() {
  TEST_ASSERT_EQUAL_UINT32(2000, backoffMs(1, 2000, 60000));
  TEST_ASSERT_EQUAL_UINT32(4000, backoffMs(2, 2000, 60000));
  TEST_ASSERT_EQUAL_UINT32(8000, backoffMs(3, 2000, 60000));
}

void test_backoff_caps() {
  TEST_ASSERT_EQUAL_UINT32(60000, backoffMs(10, 2000, 60000));
}

void test_backoff_attempt0_is_1() {
  TEST_ASSERT_EQUAL_UINT32(2000, backoffMs(0, 2000, 60000));
}

void test_shouldKick_basic() {
  RetryState r{};
  const uint32_t base = 2000, cap = 60000;

  TEST_ASSERT_FALSE(shouldKick(r, 0, base, cap));          // 0-0 < 2000
  TEST_ASSERT_FALSE(shouldKick(r, 1999, base, cap));       // 1999-0 < 2000
  TEST_ASSERT_TRUE(shouldKick(r, 2000, base, cap));        // 2000-0 >= 2000

  // nach erstem Kick: attempt=1, lastKick=2000, nächster wait=4000
  TEST_ASSERT_FALSE(shouldKick(r, 5999, base, cap));       // 5999-2000=3999 < 4000
  TEST_ASSERT_TRUE(shouldKick(r, 6000, base, cap));        // 6000-2000=4000
}

void test_onSuccess_resets() {
  RetryState r{};
  r.attempt = 7;
  r.lastKickMs = 12345;
  onSuccess(r);
  TEST_ASSERT_EQUAL_UINT8(0, r.attempt);
  // optional, falls onSuccess auch lastKickMs resetten soll:
  // TEST_ASSERT_EQUAL_UINT32(0, r.lastKickMs);
}

void run_retry_policy_tests() {
  RUN_TEST(test_backoff_basic);
  RUN_TEST(test_backoff_caps);
  RUN_TEST(test_backoff_attempt0_is_1);
  RUN_TEST(test_shouldKick_basic);
  RUN_TEST(test_onSuccess_resets);

  // + deine zusätzlichen tests hier
}