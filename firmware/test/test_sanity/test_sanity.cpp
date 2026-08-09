// ===========================================================================
//  Sanity test - proves the native test harness itself works.
//
//  Run with:  cd firmware && pio test -e native
//
//  This compiles for YOUR COMPUTER, not the Teensy. That is the whole point:
//  the pure-logic parts of the firmware (clock arithmetic, event ordering,
//  scheduling maths) can be checked in seconds with no hardware attached.
// ===========================================================================

#include <unity.h>   // the test framework PlatformIO uses for embedded C/C++

// Called before each test. Nothing to set up yet.
void setUp(void) {}

// Called after each test. Nothing to tear down yet.
void tearDown(void) {}

// A single trivial test. If this passes, the harness is wired correctly.
void test_harness_runs(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(int, char **) {
    UNITY_BEGIN();              // start the test run
    RUN_TEST(test_harness_runs);
    return UNITY_END();         // report results, return non-zero on failure
}
