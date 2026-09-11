// ===========================================================================
//  Clock.h - Step 2.1: rollover-safe microsecond clock
//
//  Purpose: give the rest of the firmware ONE safe way to answer the
//  question "has this scheduled time arrived yet?". Every scheduled tap,
//  every timeout, every "check again later" decision in this project
//  eventually calls clk::is_due() from this file.
//
//  Why this needs care at all: the Teensy's microsecond counter is only
//  32 bits wide, so it counts up to 4,294,967,295 and then wraps back to
//  zero - about every 71.58 minutes. A real recording session runs longer
//  than that. So we cannot compare timestamps the naive way (`now >= t`):
//  it silently gives the WRONG answer for a window around every wrap.
// ===========================================================================

#pragma once
// ^ "Only paste this file's contents in once per compiled file."
//   Without it, if two different .cpp files both #include "Clock.h",
//   the compiler would see everything twice and complain about
//   duplicate definitions.

#include <cstdint>
// ^ Gives us uint32_t and int32_t: integer types where we know EXACTLY
//   how many bits they have. Ordinary "int" doesn't guarantee a size,
//   and every argument in this file depends on it being exactly 32.

// ---------------------------------------------------------------------------
// We build this project two different ways:
//
//   1. For the real Teensy    -> micros() exists, provided by Arduino
//   2. For desktop unit tests -> there is no Teensy and no micros()
//
// platformio.ini's [env:native] section defines the flag UNIT_TEST, so the
// lines below let us include Arduino.h ONLY in the real build. (An #include
// must always sit outside a namespace, which is why this is up here.)
// ---------------------------------------------------------------------------
#ifndef UNIT_TEST
#include <Arduino.h> // real build only: this is where micros() comes from
#endif

namespace clk
{
  // A namespace is a named box for related things, so our "now_us" can never
  // clash with some other "now_us" defined by a library. Used from elsewhere
  // as clk::now_us().

#ifdef UNIT_TEST
  // ---------------------------------------------------------------------------
  // TEST BUILD ONLY: our stand-in for the hardware clock.
  //
  // In tests there is no real counter ticking, so instead we let the test set
  // this value directly to "whatever time we want it to be". That is what
  // makes it possible to test the rollover case at all: we can simply SAY
  // "pretend it is now 4,294,967,290" without waiting 71 real minutes.
  //
  // This lives INSIDE namespace clk so that tests refer to it the same way
  // they refer to everything else here: clk::g_fake_micros.
  // ---------------------------------------------------------------------------
  inline uint32_t g_fake_micros = 0;
#endif

  // ---------------------------------------------------------------------------
  // Returns the current time in microseconds since power-on.
  // Wraps back to 0 roughly every 71.58 minutes (that is 2^32 microseconds),
  // because the value is held in a 32-bit unsigned integer.
  // ---------------------------------------------------------------------------
  inline uint32_t now_us()
  {
#ifdef UNIT_TEST
    return g_fake_micros; // test build: whatever the test set it to
#else
    return micros(); // real build: read the Teensy's hardware counter
#endif
  }

  // ---------------------------------------------------------------------------
  // Answers: "has the scheduled time `t` arrived yet, given that it is now
  // `now`?"  Correct even when the 32-bit counter has wrapped in between.
  //
  // THE ANALOGY (a 12-hour clock face):
  //
  //   It is 11 o'clock. Something is scheduled for 1 o'clock.
  //   Has it happened yet?
  //
  //   Comparing the raw NUMBERS says no: 1 is smaller than 11.
  //   But we know 1 o'clock comes AFTER 11 o'clock - the clock wrapped past 12.
  //
  //   The way humans actually solve this is not by comparing 1 and 11.
  //   It is by asking "how far apart are they?"  From 11 to 1 is 2 hours.
  //   A small gap means it just happened. That is exactly what we do below.
  //
  // THE ARITHMETIC:
  //
  //   Step 1 - subtract, to get the gap.
  //   Unsigned subtraction in C++ wraps around too, and that saves us:
  //
  //     t   = 4294967290    (10 microseconds before the counter's maximum)
  //     now =         10    (counter wrapped; 16 us have really passed)
  //
  //     now - t = 10 - 4294967290
  //             = -4294967280                   (plain maths)
  //             = -4294967280 + 4294967296      (unsigned wraps: add 2^32)
  //             =          16                   <- the TRUE elapsed time
  //
  //   Step 2 - decide from the gap.
  //   A small gap (16) means "it just happened".
  //   But if `t` were genuinely still in the FUTURE, the same subtraction
  //   would produce a gigantic number close to 4 billion instead.
  //
  //   So we need to tell "small" apart from "gigantic". The trick: read the
  //   same 32 bits as a SIGNED number. Signed numbers use the top half of
  //   the range to mean "negative". So:
  //
  //       small number (top bit 0)      -> stays positive -> IT IS DUE
  //       gigantic number (top bit 1)   -> reads negative -> NOT DUE YET
  //
  //   One comparison handles the normal case, the future case, AND the
  //   rollover case, with no special handling for any of them.
  //
  // LIMIT: valid while the true gap is under 2^31 microseconds (~35.8
  // minutes). Every wait in this project is far under a minute, so we are
  // comfortably inside that.
  // ---------------------------------------------------------------------------
  inline bool is_due(uint32_t t, uint32_t now)
  {
    return (int32_t)(now - t) >= 0;
    // The (int32_t) cast does NOT change any bits. It only changes how the
    // same 32 bits are READ - unsigned (0 to 4 billion) versus signed
    // (-2 billion to +2 billion).
  }

  // ---------------------------------------------------------------------------
  // True if time `a` comes strictly BEFORE time `b`.
  //
  // Same reasoning as is_due() above: subtract first (unsigned subtraction
  // wraps correctly across a rollover), then read the result as SIGNED so that
  // "b is later than a" comes out negative.
  //
  //   a = 4294967290   (just before the counter wraps)
  //   b =         10   (just after it wrapped - so b is genuinely LATER)
  //
  //   a - b = 4294967290 - 10 = 4294967280
  //         read as signed  ->  -16   -> negative -> a is before b  ✓
  //
  // A plain `a < b` would answer FALSE here, ordering the two events wrongly.
  // This is why the event queue compares times with before() and never `<`.
  //
  // LIMIT: valid while a and b are within 2^31 microseconds (~35.8 minutes)
  // of each other. Everything queued during a trial spans about 15 seconds.
  // ---------------------------------------------------------------------------
  inline bool before(uint32_t a, uint32_t b)
  {
    return (int32_t)(a - b) < 0;
  }

} // namespace clk
