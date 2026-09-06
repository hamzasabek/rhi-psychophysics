// ===========================================================================
//  EventQueue.h - Step 2.2: a fixed-capacity priority queue of timed events
//
//  WHAT THIS IS FOR
//
//  A single trial schedules about 25 separate actions, each at a precise
//  moment: for every one of 6 taps, each of the 2 arms needs a "move the tip
//  over the site" and a "make the tap". Their order interleaves differently
//  for every asynchrony condition.
//
//  Rather than writing tangled if-statements to work out what to do next,
//  we build a LIST of timestamped actions up front, and loop() becomes:
//
//      is anything due?  ->  yes: do it.  ->  repeat.
//
//  This file is that list. You put events in in any order; it always hands
//  back the earliest one first.
//
//  HOW IT WORKS: a binary min-heap.
//
//  A plain array, with one rule imposed on it:
//
//      every parent holds an EARLIER time than both of its children
//
//  where "parent" and "child" are defined purely by arithmetic on the index:
//
//      children of index i  ->  2i + 1  and  2i + 2
//      parent   of index i  ->  (i - 1) / 2
//
//  There is no actual tree in memory - no pointers, no allocation. Just an
//  array and that rule. The rule guarantees the earliest event is always at
//  index 0, so finding "what's next" costs nothing.
//
//  The array is NOT sorted. It is a much weaker (and therefore much cheaper)
//  arrangement than sorting, and it is exactly as much order as we need.
//
//  NO ALLOCATION: capacity is fixed at compile time via a template, so the
//  memory is reserved once at startup and can never fragment or fail
//  mid-session. See PROJECT.md section 4.3.
// ===========================================================================

#pragma once
// ^ Only paste this file in once per compiled file, however many times it
//   gets #included.

#include <cstdint>   // exact-width integer types: uint32_t, uint8_t
#include <cstddef>   // size_t: an unsigned type big enough for any array index
#include "Clock.h"   // our own module: clk::before() and clk::is_due()

// ---------------------------------------------------------------------------
// What kind of thing an event is.
//
// `enum class` creates a named set of options that the compiler checks. We
// use it instead of plain numbers so that writing EventType::Tap is
// impossible to confuse with an arm number or a site index - the compiler
// simply refuses to mix them up.
//
// The `: uint8_t` part says "store these in a single byte". We do not need
// more, and on a microcontroller every byte in a 64-slot array counts.
// ---------------------------------------------------------------------------
enum class EventType : uint8_t {
  None     = 0,   // "nothing" - returned when popping an empty queue
  Select,         // distal joint: swing the tip over a finger site (slow)
  Tap,            // proximal joint: descend and make contact (timing-critical)
  TrialEnd,       // the stimulation period is over
};

// ---------------------------------------------------------------------------
// One scheduled action.
//
// A `struct` is simply a bundle of related values given a name, so we can
// pass all four around together as one thing.
//
// Field order matters slightly here: putting the 4-byte t_us first and the
// three 1-byte fields after keeps the whole struct to 8 bytes with no
// wasted padding.
// ---------------------------------------------------------------------------
struct Event {
  uint32_t  t_us;   // WHEN this should happen (microseconds, clk's time base)
  EventType type;   // WHAT should happen
  uint8_t   arm;    // WHICH arm: 0 = real hand, 1 = rubber hand
  uint8_t   arg;    // extra detail; meaning depends on `type`
                    //   for Select: which of the 5 finger sites (0..4)
                    //   for Tap:    unused, set to 0
};

// ---------------------------------------------------------------------------
// The queue itself.
//
// `template <size_t Capacity>` means this is not one class but a RECIPE.
// Writing EventQueue<64> tells the compiler to generate a class with a
// 64-slot array; EventQueue<4> generates a different class with 4 slots.
// This all happens at compile time, so there is no runtime cost and no
// memory allocation while the program is running.
// ---------------------------------------------------------------------------
template <size_t Capacity>
class EventQueue {
 public:
  // ---- Adding -------------------------------------------------------------

  // Add an event. Returns false if the queue is already full.
  //
  // We return a value rather than silently dropping the event, because on an
  // embedded device failing LOUDLY is far better than failing cleverly. The
  // caller is expected to check the result.
  bool push(const Event& e) {
    // `const Event& e` means "give me a reference to the caller's event, do
    // not copy it, and I promise not to modify it". The & avoids copying
    // 8 bytes; the const is a promise the compiler enforces.

    if (n_ >= Capacity) {
      return false;               // full - refuse rather than overrun memory
    }

    heap_[n_] = e;                // place the new event in the first free slot
    const size_t placed_at = n_;  // remember where we put it
    ++n_;                         // the queue is now one item longer
    sift_up(placed_at);           // let it climb to its correct level
    return true;
  }

  // ---- Inspecting ---------------------------------------------------------

  // Is the earliest event due yet, given the current time?
  //
  // Returns false for an empty queue - "nothing to do" rather than an error,
  // which is what loop() wants to hear the vast majority of the time.
  bool peek_due(uint32_t now) const {
    // `const` after the parameter list promises this function does not change
    // anything inside the object. The compiler enforces it.
    if (n_ == 0) {
      return false;
    }
    return clk::is_due(heap_[0].t_us, now);
    // heap_[0] is ALWAYS the earliest, thanks to the parent-before-child rule
  }

  // ---- Removing -----------------------------------------------------------

  // Remove and return the earliest event.
  //
  // Popping an empty queue is SAFE: it returns an Event whose type is None,
  // rather than crashing or returning garbage. Callers can check for that.
  Event pop() {
    if (n_ == 0) {
      return Event{0, EventType::None, 0, 0};   // the "nothing" event
    }

    const Event earliest = heap_[0];   // this is the answer we will return

    --n_;                              // the queue is now one item shorter
    if (n_ > 0) {
      heap_[0] = heap_[n_];            // move the LAST item into the hole
      sift_down(0);                    // and let it sink to its right level
    }

    return earliest;
  }

  // ---- Housekeeping -------------------------------------------------------

  // Throw everything away. Used when a trial is aborted.
  //
  // Note we only reset the COUNT, not the array contents. Anything above n_
  // is unreachable through the public interface, so zeroing it would be
  // wasted work.
  void clear() { n_ = 0; }

  size_t size()  const { return n_; }               // how many events are queued
  bool   empty() const { return n_ == 0; }
  bool   full()  const { return n_ >= Capacity; }

 private:
  // Everything below is internal. Nothing outside this class can touch it.
  // A small public surface is a small thing to get wrong.

  Event  heap_[Capacity];   // the storage: a plain fixed-size array
  size_t n_ = 0;            // how many slots are currently in use

  // -------------------------------------------------------------------------
  // Is event `a` earlier than event `b`?
  //
  // This MUST use clk::before() and never the `<` operator. Timestamps wrap
  // around every ~71 minutes, and a plain `<` puts events on opposite sides
  // of a wrap into the wrong order. See the comment on clk::before().
  //
  // `static` means this helper belongs to the class as a whole rather than
  // to any particular queue object - it does not read or change any fields.
  // -------------------------------------------------------------------------
  static bool earlier(const Event& a, const Event& b) {
    return clk::before(a.t_us, b.t_us);
  }

  // Exchange the events at two positions.
  void swap_at(size_t i, size_t j) {
    const Event tmp = heap_[i];
    heap_[i] = heap_[j];
    heap_[j] = tmp;
  }

  // -------------------------------------------------------------------------
  // BUBBLE UP: a newly-added event may be earlier than its parent, which
  // breaks the rule. Keep swapping it upward until it is in the right place.
  //
  // Worked example - pushing time 150 into [100, 300, 200, 700, ...]:
  //   the new item lands at index 7; its parent is index 3, holding 700
  //   150 is earlier than 700  -> swap, now at index 3
  //   parent is index 1, holding 300
  //   150 is earlier than 300  -> swap, now at index 1
  //   parent is index 0, holding 100
  //   150 is NOT earlier than 100 -> stop, the rule holds again
  // -------------------------------------------------------------------------
  void sift_up(size_t i) {
    while (i > 0) {                      // index 0 has no parent - nowhere to go
      const size_t parent = (i - 1) / 2;

      if (!earlier(heap_[i], heap_[parent])) {
        break;                           // already in the right place
      }

      swap_at(i, parent);
      i = parent;                        // follow the item up and check again
    }
  }

  // -------------------------------------------------------------------------
  // SINK DOWN: after a pop, the last item was moved to the top and is
  // almost certainly too late to be there. Keep swapping it downward - each
  // time with the EARLIER of its two children - until it settles.
  //
  // Swapping with the earlier child matters: swapping with the later one
  // would put a late event above an early one and break the rule again.
  // -------------------------------------------------------------------------
  void sift_down(size_t i) {
    for (;;) {                                  // loop until we break out
      const size_t left  = 2 * i + 1;
      const size_t right = 2 * i + 2;
      size_t earliest = i;                      // assume we are already correct

      // Does a left child exist, and is it earlier than our best so far?
      if (left < n_ && earlier(heap_[left], heap_[earliest])) {
        earliest = left;
      }
      // Same question for the right child.
      if (right < n_ && earlier(heap_[right], heap_[earliest])) {
        earliest = right;
      }

      if (earliest == i) {
        break;                                  // neither child is earlier - done
      }

      swap_at(i, earliest);
      i = earliest;                             // follow the item down
    }
  }
};



