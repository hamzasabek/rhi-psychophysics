// ===========================================================================
//  test_clock.cpp - Step 2.1: prove clk::is_due() is correct, INCLUDING at
//  the 32-bit rollover boundary that we can never conveniently reach by
//  actually waiting on real hardware.
//
//  Run with:  cd firmware && pio test -e native
// ===========================================================================

#include <unity.h>
#include "Clock.h"
// ^ Because platformio.ini's [env:native] section defines UNIT_TEST, this
//   pulls in the FAKE clock (g_fake_micros) rather than the real Arduino
//   micros(). That is what lets us set "the current time" directly, to
//   any value we choose - including values right at the rollover edge.

void setUp(void)
{
  // Runs before EVERY test below. Resetting the fake clock here means each
  // test starts from a known state and cannot accidentally be affected by
  // whatever value a PREVIOUS test left behind.
  clk::g_fake_micros = 0;
}

void tearDown(void) {} // nothing to clean up after each test

// ---------------------------------------------------------------------------
// The ordinary, no-rollover-involved case: scheduled time is in the past,
// "now" is later. This should simply be true - the everyday case that
// makes up the vast majority of real is_due() calls during a session.
// ---------------------------------------------------------------------------
void test_is_due_true_when_time_has_passed(void)
{
  uint32_t t = 1000;   // scheduled for 1000 us after power-on
  uint32_t now = 1500; // we are asking at 1500 us - 500 us later
  TEST_ASSERT_TRUE(clk::is_due(t, now));
}

// ---------------------------------------------------------------------------
// The mirror case: scheduled time is still in the future. Must be false -
// getting this wrong would mean taps fire early, which is just as bad as
// firing late.
// ---------------------------------------------------------------------------
void test_is_due_false_when_time_is_future(void)
{
  uint32_t t = 5000;   // scheduled for 5000 us
  uint32_t now = 1000; // but we're only at 1000 us - hasn't happened yet
  TEST_ASSERT_FALSE(clk::is_due(t, now));
}

// ---------------------------------------------------------------------------
// The boundary case: now == t, exactly. "Due" should include the exact
// instant, not just strictly-after - otherwise an event scheduled for
// precisely the moment we happen to check would be skipped by one poll of
// the loop.
// ---------------------------------------------------------------------------
void test_is_due_true_at_exact_moment(void)
{
  uint32_t t = 42000;
  uint32_t now = 42000; // exactly equal
  TEST_ASSERT_TRUE(clk::is_due(t, now));
}

// ---------------------------------------------------------------------------
// THE important test. Constructs the exact scenario from the worked
// example in Clock.h: a scheduled time just BEFORE the 32-bit counter's
// maximum value, and "now" just AFTER it has wrapped back around to a
// small number. In true elapsed time, 16 microseconds have passed and the
// event is due - but a naive `now >= t` comparison would see a tiny `now`
// and a huge `t` and wrongly conclude the event is still far in the
// future. This is precisely the bug this whole file exists to prevent.
// ---------------------------------------------------------------------------
void test_is_due_survives_rollover(void)
{
  uint32_t t = 4294967290u; // 10 microseconds before the counter wraps
  uint32_t now = 10u;       // counter has wrapped; 16 us have truly passed
  TEST_ASSERT_TRUE(clk::is_due(t, now));
}

// ---------------------------------------------------------------------------
// The other side of the same boundary: scheduled time is AFTER the wrap,
// "now" is a moment BEFORE it that hasn't reached the wrap yet. The event
// must correctly read as still in the future, even though t's raw numeric
// value (small) looks smaller than now's raw numeric value (huge) - the
// opposite of what a naive comparison would assume.
// ---------------------------------------------------------------------------
void test_is_due_false_just_before_rollover(void)
{
  uint32_t t = 20u;           // scheduled for just after the wrap
  uint32_t now = 4294967290u; // we are still 30 us before the wrap happens
  TEST_ASSERT_FALSE(clk::is_due(t, now));
}

int main(int, char **)
{
  UNITY_BEGIN();
  RUN_TEST(test_is_due_true_when_time_has_passed);
  RUN_TEST(test_is_due_false_when_time_is_future);
  RUN_TEST(test_is_due_true_at_exact_moment);
  RUN_TEST(test_is_due_survives_rollover);
  RUN_TEST(test_is_due_false_just_before_rollover);
  return UNITY_END();
}
