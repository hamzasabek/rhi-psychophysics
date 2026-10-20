// ===========================================================================
//  HostLink.h - the "front panel" of the module that talks to the host PC
//
//  This header ANNOUNCES what the HostLink module offers, without showing
//  how it works inside. Other files (like main.cpp) read this to learn how
//  to use the module; the actual code lives in HostLink.cpp.
//
//  What HostLink does: it watches the USB serial link, assembles incoming
//  characters into whole lines, recognises commands (currently just PING),
//  and sends the matching replies. It never blocks.
//
//  Layer 3 of Step 2.3 hardens the parser against three garbage cases:
//    - Over-long lines: detected with a flag, reported rather than silently
//      truncated and parsed as a mystery fragment.
//    - Empty lines: silently discarded; not an error.
//    - Bare "PING" (the verb without a sequence number): a specific,
//      helpful error rather than the confusing generic "unknown command".
// ===========================================================================

#pragma once

#include <cstddef>   // size_t

// ---------------------------------------------------------------------------
// The HostLink class.
//
// A "class" bundles some data (here: the line buffer and its length) together
// with the functions that work on that data (begin, poll). Bundling them means
// nobody outside can fiddle with the buffer directly - they can only ask the
// module to do its defined jobs. Small door, few ways to break it.
// ---------------------------------------------------------------------------
class HostLink {
 public:
  // Called once at start-up. Opens the serial link.
  void begin();

  // Called on EVERY pass of loop(). Reads any waiting characters, and when a
  // full line has arrived, acts on it. Returns immediately if nothing is
  // waiting - never blocks.
  void poll();

 private:
  // Everything below is internal machinery. Nothing outside the class can see
  // or touch it. The trailing underscore marks these as the class's own data.

  // Handle one complete line of text that has just finished arriving.
  // Pre-condition (guaranteed by poll()): line is non-empty and null-terminated.
  void handle_line(const char* line);

  // The buffer where incoming characters are collected until a newline.
  // 128 bytes total: indices 0-126 hold content; one slot is always kept free
  // for the '\0' terminator, which poll() writes at buffer_[length_].
  static constexpr size_t BUFFER_SIZE = 128;   // 127 usable chars + terminator
  char   buffer_[BUFFER_SIZE];   // the pigeonholes for characters
  size_t length_ = 0;            // how many characters collected so far

  // True once a character has been discarded because the buffer was full.
  // Cleared to false when '\n' arrives and the line is dealt with. While set,
  // every ordinary character is discarded (buffer stays at its limit) and the
  // eventual '\n' emits ERR rather than calling handle_line() on a fragment.
  bool overflow_ = false;
};
