// ===========================================================================
//  test_event_queue.cpp - Step 2.2: prove the event queue behaves correctly
//
//  Run with:  cd firmware && pio test -e native
//
//  What we check, and why each one earns its place:
//    - a fresh queue is empty                  (the starting state is sane)
//    - events come out earliest-first          (the whole point of the class)
//    - a full queue refuses politely           (fails loudly, not silently)
//    - popping an empty queue is safe          (no crash, no garbage)
//    - ordering survives a clock rollover      (the subtle one)
//    - 200 shuffled events come out in order   (stress: the heap really works)
// ===========================================================================

#include <unity.h>
#include "EventQueue.h"

// A small helper so the tests read cleanly. Most tests only care about the
// time, so this fills in sensible values for the other fields.
static Event ev(uint32_t t_us) {
  return Event{t_us, EventType::Tap, 0, 0};
}

void setUp(void) {}      // each test makes its own queue, so nothing to reset
void tearDown(void) {}

// ---------------------------------------------------------------------------
// A queue that has just been created should report itself as empty, with
// nothing due. Trivial, but it pins down the starting state - and a class
// that got THIS wrong would produce very confusing failures elsewhere.
// ---------------------------------------------------------------------------
void test_new_queue_is_empty(void) {
  EventQueue<8> q;
  TEST_ASSERT_TRUE(q.empty());
  TEST_ASSERT_EQUAL_UINT32(0, q.size());
  TEST_ASSERT_FALSE(q.peek_due(1000000));   // nothing queued, so nothing is due
}

// ---------------------------------------------------------------------------
// Pushing one event should make the queue non-empty and hand that same event
// straight back.
// ---------------------------------------------------------------------------
void test_push_then_pop_returns_the_same_event(void) {
  EventQueue<8> q;
  TEST_ASSERT_TRUE(q.push(Event{5000, EventType::Select, 1, 3}));
  TEST_ASSERT_EQUAL_UINT32(1, q.size());

  const Event out = q.pop();
  TEST_ASSERT_EQUAL_UINT32(5000, out.t_us);
  TEST_ASSERT_EQUAL_UINT8(1, out.arm);
  TEST_ASSERT_EQUAL_UINT8(3, out.arg);
  TEST_ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// THE CORE TEST. Events are pushed in deliberately jumbled order; they must
// come out earliest-first regardless. If the heap logic is wrong anywhere -
// sift_up, sift_down, or the comparison - this test fails.
// ---------------------------------------------------------------------------
void test_events_come_out_in_time_order(void) {
  EventQueue<8> q;
  q.push(ev(500));
  q.push(ev(100));
  q.push(ev(900));
  q.push(ev(300));
  q.push(ev(700));

  TEST_ASSERT_EQUAL_UINT32(100, q.pop().t_us);
  TEST_ASSERT_EQUAL_UINT32(300, q.pop().t_us);
  TEST_ASSERT_EQUAL_UINT32(500, q.pop().t_us);
  TEST_ASSERT_EQUAL_UINT32(700, q.pop().t_us);
  TEST_ASSERT_EQUAL_UINT32(900, q.pop().t_us);
  TEST_ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// A full queue must REFUSE new events by returning false, rather than
// writing past the end of its array. Writing past the end would silently
// corrupt whatever memory happened to sit next to it - the kind of bug that
// shows up as an unrelated crash hours later.
//
// A capacity of 2 makes this easy to reach on purpose. This is exactly why
// the class is a template: tests can pick a tiny size.
// ---------------------------------------------------------------------------
void test_full_queue_refuses_new_events(void) {
  EventQueue<2> q;
  TEST_ASSERT_TRUE(q.push(ev(100)));    // slot 1 of 2 - fine
  TEST_ASSERT_TRUE(q.push(ev(200)));    // slot 2 of 2 - fine
  TEST_ASSERT_TRUE(q.full());
  TEST_ASSERT_FALSE(q.push(ev(300)));   // no room - must refuse
  TEST_ASSERT_EQUAL_UINT32(2, q.size());  // and must NOT have grown
}

// ---------------------------------------------------------------------------
// Popping an empty queue must not crash and must not return garbage. It
// returns an event marked None, which callers can recognise.
// ---------------------------------------------------------------------------
void test_popping_empty_queue_is_safe(void) {
  EventQueue<8> q;
  const Event out = q.pop();
  TEST_ASSERT_TRUE(out.type == EventType::None);
  TEST_ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// peek_due should only say "yes" once the earliest event's time has arrived,
// and should keep saying "yes" afterwards.
// ---------------------------------------------------------------------------
void test_peek_due_respects_the_clock(void) {
  EventQueue<8> q;
  q.push(ev(1000));

  TEST_ASSERT_FALSE(q.peek_due(999));    // one microsecond too early
  TEST_ASSERT_TRUE(q.peek_due(1000));    // exactly on time counts as due
  TEST_ASSERT_TRUE(q.peek_due(5000));    // and it stays due afterwards
}

// ---------------------------------------------------------------------------
// THE SUBTLE ONE. Two events sit on opposite sides of the 32-bit counter's
// wrap. In real time, A (just before the wrap) happens BEFORE B (just after).
//
// But as raw numbers, A is about 4 billion and B is 10 - so a naive `<`
// comparison would order them backwards and fire B's tap first. This test
// is what stops that.
// ---------------------------------------------------------------------------
void test_ordering_survives_rollover(void) {
  EventQueue<8> q;
  q.push(ev(10u));            // just AFTER the wrap - happens second
  q.push(ev(4294967290u));    // just BEFORE the wrap - happens first

  TEST_ASSERT_EQUAL_UINT32(4294967290u, q.pop().t_us);   // must come out first
  TEST_ASSERT_EQUAL_UINT32(10u,         q.pop().t_us);
}

// ---------------------------------------------------------------------------
// STRESS TEST. 200 events in scrambled order must all come out in
// non-decreasing time order.
//
// Note the deliberately simple, hand-rolled number generator below. It is
// not a good random source, but it IS reproducible: it produces exactly the
// same sequence every run. A test that used real randomness could fail once
// in a hundred runs and be nearly impossible to reproduce - so tests must
// always be deterministic.
// ---------------------------------------------------------------------------
void test_many_scrambled_events_come_out_in_order(void) {
  EventQueue<256> q;

  uint32_t seed = 12345u;
  for (int i = 0; i < 200; ++i) {
    // A "linear congruential generator": multiply, add, keep the low bits.
    // Crude, but perfectly adequate for scrambling an order.
    seed = seed * 1664525u + 1013904223u;
    const uint32_t t = seed % 1000000u;   // spread over 0..999999 microseconds
    TEST_ASSERT_TRUE(q.push(ev(t)));
  }

  TEST_ASSERT_EQUAL_UINT32(200, q.size());

  uint32_t previous = 0;
  for (int i = 0; i < 200; ++i) {
    const uint32_t t = q.pop().t_us;
    // Each event must be at or after the one before it. All times here are
    // well under 2^31 and none wrap, so a plain >= is safe in this test.
    TEST_ASSERT_TRUE(t >= previous);
    previous = t;
  }

  TEST_ASSERT_TRUE(q.empty());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_new_queue_is_empty);
  RUN_TEST(test_push_then_pop_returns_the_same_event);
  RUN_TEST(test_events_come_out_in_time_order);
  RUN_TEST(test_full_queue_refuses_new_events);
  RUN_TEST(test_popping_empty_queue_is_safe);
  RUN_TEST(test_peek_due_respects_the_clock);
  RUN_TEST(test_ordering_survives_rollover);
  RUN_TEST(test_many_scrambled_events_come_out_in_order);
  return UNITY_END();
}
