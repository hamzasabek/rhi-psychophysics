// ===========================================================================
//  main.cpp - the program's entry point: setup() runs once, loop() forever
//
//  An Arduino program has no main() of its own: the framework supplies one,
//  and what it does amounts to this:
//
//      setup();                  // once, at power-up or after a reset
//      for (;;) { loop(); }      // then forever, as fast as possible
//
//  So setup() is "start-up" and loop() is "the body of an endless loop".
//
//  This file stays a thin wiring file. Every real job lives in its own
//  module (PROJECT.md section 0.5, "one file, one job"). Today the only
//  module is HostLink (Step 2.3). The event queue, the two robot arms and
//  the trial runner get wired in here as the roadmap builds them.
//
//  loop() must never wait for anything: no delay(), ever (CLAUDE.md rule 2).
//  Each pass takes only microseconds, which is what will let a scheduled tap
//  be noticed within microseconds of becoming due.
// ===========================================================================

#include <Arduino.h>   // the framework: declares setup() and loop(), and Serial
#include "HostLink.h"  // our module that talks to the host PC over USB

// ---- The modules this program is made of ----------------------------------

// The one HostLink for the whole program. It is created before setup() runs,
// and its line buffer is part of the object, so nothing is allocated while
// the program runs (CLAUDE.md rule 3). Only this file touches it.
HostLink host;

// ---------------------------------------------------------------------------
// setup(): runs once, at power-up or after a reset.
// ---------------------------------------------------------------------------
void setup() {
  host.begin();   // open the USB serial link to the host PC
}

// ---------------------------------------------------------------------------
// loop(): runs forever, one quick pass after another.
// ---------------------------------------------------------------------------
void loop() {
  host.poll();    // act on any characters that arrived; returns at once if none
}
