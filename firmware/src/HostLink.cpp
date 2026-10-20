// ===========================================================================
//  HostLink.cpp - the "insides" of the host-communication module
//
//  This file implements what HostLink.h announced. It contains the actual
//  character-collecting, line-assembling, and command-handling code.
//
//  Step 2.3, Layer 3: harden the parser against three common garbage inputs.
//
//  Problem 1 — over-long lines.
//    A USB serial port will deliver arbitrarily long text. Previously,
//    characters past byte 127 were silently dropped, leaving a truncated
//    fragment in the buffer that handle_line() would then try to parse. In
//    the worst case the fragment could look like a valid command. Now we set
//    an overflow flag on the first discarded character, keep discarding until
//    '\n', and then report ERR instead of parsing the fragment.
//
//  Problem 2 — empty lines.
//    A bare '\n' — from a terminal's Enter with nothing typed, or the second
//    byte of a Windows \r\n pair after the \r is stripped — produced
//    "ERR unknown command: (nothing)". Now it is silently ignored in poll()
//    before handle_line() is called.
//
//  Problem 3 — bare PING (no sequence number).
//    "PING 42" is valid; "PING" alone is a formatting mistake. Previously it
//    fell through to the generic "unknown command" error. Now it gets a
//    specific message: ERR PING needs a sequence number, e.g. PING 1.
// ===========================================================================

#include "HostLink.h"     // our own announcement - must match it
#include "protocol.h"     // the message contract
#include "Clock.h"        // clk::now_us() for the PONG timestamp

#include <Arduino.h>      // Serial, and the framework
#include <cstring>        // strncmp(), strcmp() - C string comparison helpers

// ---------------------------------------------------------------------------
// begin(): open the USB serial link. Called once from setup().
// ---------------------------------------------------------------------------
void HostLink::begin() {
  Serial.begin(115200);   // rate is nominal on Teensy USB, set by convention
}

// ---------------------------------------------------------------------------
// poll(): called every loop(). Collect any waiting characters; when a full
// line has arrived (marked by '\n'), decide what to do with it.
//
// Three outcomes when '\n' arrives:
//   a) overflow_ is set  → the line was too long; emit ERR, clear, carry on
//   b) length_ == 0      → empty line; silently discard
//   c) normal line       → null-terminate and hand to handle_line()
//
// Worked example — a host sends 200 bytes of garbage with a '\n' at the end:
//   bytes 1-127:  stored in buffer_[0..126], length_ counts up to 127
//   byte 128:     length_ == BUFFER_SIZE - 1 (127); can't store it →
//                 overflow_ = true (flag goes up), character discarded
//   bytes 129-200: overflow_ is already true; each hits the same branch and
//                  sets overflow_ = true again (already true, so a no-op)
//   '\n' arrives: overflow_ is set → print ERR, clear overflow_ and length_
//                 handle_line() is NEVER called on the fragment in buffer_
// ---------------------------------------------------------------------------
void HostLink::poll() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();   // take one waiting character

    if (c == '\n') {
      // -----------------------------------------------------------------------
      // End of line: handle all three outcomes in priority order.
      // -----------------------------------------------------------------------

      if (overflow_) {
        // The line was too long for the buffer. We already discarded the excess
        // characters as they arrived; now that the line has ended we can say so.
        // We do NOT call handle_line(): the buffer holds only the first 127
        // characters of whatever was sent, which is a meaningless fragment.
        Serial.print("ERR line too long, max ");
        Serial.print(BUFFER_SIZE - 1);    // 127: indices 0-126 are usable content
        Serial.println(" chars");
        overflow_ = false;                // clear the flag for the next line
        length_   = 0;                    // reset the write position

      } else if (length_ == 0) {
        // Empty line (nothing arrived between the last '\n' and this one).
        // This is normal and harmless: a terminal often sends \r\n for Enter;
        // the \r is stripped below, leaving a bare \n that looks like a blank
        // line. Silently ignore it. length_ is already 0, nothing to reset.

      } else {
        // Normal, non-empty line that fit entirely in the buffer.
        buffer_[length_] = '\0';    // place the C string terminator
        handle_line(buffer_);       // act on it
        length_ = 0;                // reset the write position for next line
      }

    } else if (c == '\r') {
      // Carriage return: silently ignored. Windows sends \r\n for Enter; we
      // strip the \r here so handle_line() never has to deal with it.

    } else {
      // -----------------------------------------------------------------------
      // Ordinary character: store it if there is room, otherwise arm the flag.
      // -----------------------------------------------------------------------
      if (length_ < BUFFER_SIZE - 1) {
        // There is space in the buffer.  Store the character and advance the
        // write position. (BUFFER_SIZE - 1 == 127 because buffer_[127] is
        // reserved for the '\0' terminator; we must never overwrite it with
        // content.)
        buffer_[length_] = c;
        length_ = length_ + 1;
      } else {
        // Buffer is full and the line has still not ended. The character cannot
        // be stored — doing so would corrupt the terminator slot at buffer_[127].
        // Set the overflow flag. Every subsequent character will hit this same
        // branch; re-setting overflow_ to true is a no-op, which is fine.
        overflow_ = true;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// handle_line(): decide what a completed line means and respond.
//
// Pre-condition (guaranteed by poll()): line is non-empty and null-terminated.
//
// `const char* line` is "a pointer to the first character of the line". The
// `const` promises we will not modify the text. This is how C passes a string:
// not the characters themselves, but the address of where they start (the
// characters end at the '\0' that poll() placed at buffer_[length_]).
// ---------------------------------------------------------------------------
void HostLink::handle_line(const char* line) {

  // -- PING <seq> -----------------------------------------------------------
  //
  // strncmp(a, b, n): compare the first n characters of two strings; returns
  // 0 when they match, non-zero otherwise. We check for "PING " — five chars,
  // including the trailing space — so that both the verb AND the separator are
  // present before we accept it. Without the space check, "PINGWHATEVER" would
  // also match.
  //
  // Why not "=="? In C you cannot compare strings with ==. That would compare
  // two MEMORY ADDRESSES (always unequal), not the characters. strncmp walks
  // through the characters one by one.
  //
  // Worked example: line = "PING 7"
  //   strncmp("PING 7", "PING ", 5) compares P,I,N,G,' ' → all match → 0
  //   seq_text = line + 5 → points at '7' (then '\0')
  //   output: PONG 7 t_us=3141592
  if (strncmp(line, "PING ", 5) == 0) {
    // The sequence number starts at character position 5 (right after "PING ").
    // `line + 5` is pointer arithmetic: "the address 5 bytes further along".
    const char* seq_text = line + 5;     // points at the first digit (or char)
    Serial.print("PONG ");
    Serial.print(seq_text);              // echo the sequence number unchanged
    Serial.print(" t_us=");
    Serial.println(clk::now_us());       // current microsecond clock reading
    return;                              // handled; stop here
  }

  // -- Bare "PING" (verb present, sequence number absent) -------------------
  //
  // "PING" alone does not match the branch above because there is no space
  // after the verb. It is a recognisable formatting mistake — the user knows
  // the right command, they just forgot the number — so we give a specific
  // message instead of the generic "unknown command".
  //
  // strcmp(a, b): FULL string comparison (both characters and the '\0'). This
  // is the right tool here: we want to match exactly "PING" and nothing else.
  // strncmp(line, "PING", 4) would also match "PING2", "PING-FOO", etc.,
  // which should fall through to the generic error below.
  if (strcmp(line, "PING") == 0) {
    Serial.println("ERR PING needs a sequence number, e.g. PING 1");
    return;
  }

  // -- Unrecognised command --------------------------------------------------
  //
  // We never crash or freeze on input we do not understand. We say what we saw
  // and carry on, ready for the next line.
  Serial.print("ERR unknown command: ");
  Serial.println(line);
}
