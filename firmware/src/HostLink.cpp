// ===========================================================================
//  HostLink.cpp - the "insides" of the host-communication module
//
//  This file implements what HostLink.h announced. It contains the actual
//  character-collecting, line-assembling, and command-handling code.
//
//  Layer 2 of Step 2.3: recognise the PING command and reply with PONG plus
//  the current clock reading. (Layer 3 will add robust handling of garbage.)
// ===========================================================================

#include "HostLink.h"     // our own announcement - must match it
#include "protocol.h"     // the message contract
#include "Clock.h"        // clk::now_us() for the PONG timestamp

#include <Arduino.h>      // Serial, and the framework
#include <cstring>        // strncmp() and strlen() - C text helpers

// ---------------------------------------------------------------------------
// begin(): open the USB serial link. Called once from setup().
// ---------------------------------------------------------------------------
void HostLink::begin() {
  Serial.begin(115200);
  // The number is nominal on Teensy USB, set by convention. See warm-up notes.
}

// ---------------------------------------------------------------------------
// poll(): called every loop(). Collect any waiting characters; when a full
// line has arrived (marked by '\n'), hand it to handle_line().
//
// This is the same non-blocking collect-until-newline pattern from the
// warm-up, now living inside the module.
// ---------------------------------------------------------------------------
void HostLink::poll() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();   // take one waiting character

    if (c == '\n') {
      // Line complete. Terminate the buffer as a proper C string...
      buffer_[length_] = '\0';
      // ...act on it...
      handle_line(buffer_);
      // ...and reset for the next line.
      length_ = 0;

    } else if (c == '\r') {
      // Ignore carriage returns (Windows sends '\r\n' for Enter). See warm-up.

    } else {
      // Ordinary character: append it, leaving room for the '\0' terminator.
      if (length_ < BUFFER_SIZE - 1) {
        buffer_[length_] = c;
        length_ = length_ + 1;
      }
      // (If the line is too long we silently drop the extra characters for
      //  now. Layer 3 will handle over-long lines more gracefully.)
    }
  }
}

// ---------------------------------------------------------------------------
// handle_line(): decide what a completed line means, and respond.
//
// `const char* line` is "a pointer to the first character of the line". The
// `const` promises we will not modify the text through it. This is how C
// passes a string around: not the characters themselves, but the address of
// where they start (they end at the '\0' we placed).
// ---------------------------------------------------------------------------
void HostLink::handle_line(const char* line) {

  // -- Is this a PING? -------------------------------------------------------
  //
  // strncmp(a, b, n) compares the first n characters of two strings. It
  // returns 0 when they are equal. We check the first 5 characters against
  // "PING " (note the trailing space, so "PINGABC" does not match).
  //
  // Why strncmp and not "==": in C you cannot compare text with ==. That
  // would compare the two ADDRESSES, not the characters. strncmp actually
  // walks through and compares character by character.
  if (strncmp(line, "PING ", 5) == 0) {

    // The sequence number is the text AFTER "PING ", i.e. starting at
    // character position 5. `line + 5` is "the address 5 characters along" -
    // a pointer to where the number begins.
    const char* seq_text = line + 5;

    // Send the reply: PONG <same seq> t_us=<clock now>.
    Serial.print("PONG ");
    Serial.print(seq_text);               // echo the sequence back unchanged
    Serial.print(" t_us=");
    Serial.println(clk::now_us());        // the current microsecond clock
    return;                               // handled - done with this line
  }

  // -- Unrecognised -> report an error, but keep running. --------------------
  //
  // We never crash or freeze on input we do not understand. We say so and
  // carry on, ready for the next line.
  Serial.print("ERR unknown command: ");
  Serial.println(line);
}
