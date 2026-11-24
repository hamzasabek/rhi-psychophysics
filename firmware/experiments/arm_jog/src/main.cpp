// ===========================================================================
//  arm_jog - test program for roadmap Step 3.2 (find the site angles)
//
//  WHAT IT DOES
//  ------------
//  Drives the four servos of the two arms from typed commands in the serial
//  monitor, so the three joint settings for each finger site can be found by
//  hand: RAISED (proximal), AIM (distal) and TOUCH (proximal). See
//  docs/geometry.md and the Step 3.2 illustration.
//
//  PIN PLAN (decided 2026-11-23, checked against the Teensy core's pin table)
//  --------------------------------------------------------------------------
//    top  arm : proximal PDI-4409MG on pin 3,  distal EMAX ES9257 on pin 6
//    down arm : proximal PDI-4409MG on pin 2,  distal EMAX ES9257 on pin 9
//
//  Pins 2 and 3 are the A and B channels of ONE hardware PWM timer, so they
//  share one frequency: the PDIs run at 333 Hz. Pins 6 and 9 are the A and B
//  channels of a DIFFERENT timer: the EMAXes run at 100 Hz (Step 3.1: they
//  buzz at 200 Hz and above). Giving the two pairs different timers is what
//  lets them run at different rates. Check both on the logic analyser.
//
//  COMMANDS (type one, press Enter; upper or lower case)
//  -----------------------------------------------------
//    top  prox 1500    set one joint to a pulse in microseconds (absolute)
//    down dist -5      move one joint 5 us DOWN from where it is heading
//    down dist +5      move one joint 5 us UP   (a leading + or - means
//                      "relative"; no sign means "absolute")
//    RATE 200          slew speed in us per second, 20 to 2000 (default 200)
//    MARK site3_touch  print all four pulses on one line, to copy into notes
//    SHOW              print every joint
//    OFF               stop ALL pulses.   OFF top prox   stops one joint.
//
//  SAFETY - READ BEFORE USING WITH A FINGER UNDER THE ARM
//  ------------------------------------------------------
//  1. Nothing is sent at power-up. A joint starts only when you give it an
//     ABSOLUTE pulse. That first pulse is applied at once, because the
//     program cannot know where the servo is. Use it to give each joint a
//     pulse near where the arm already rests, then switch on the servo power
//     (pulses first, power second, as in Step 3.1).
//  2. After that, moves are slew-limited: the pulse travels toward the target
//     at RATE us per second, so the arm cannot snap onto the finger.
//  3. Each joint has a hard limit (see g_joints). A command outside it is
//     refused. The EMAX limit is the 900-1900 us range measured in Step 3.1.
//  4. OFF on a proximal joint lets that arm go limp, and it may drop. Raise it
//     and support it first.
//
//  HOW A PULSE IS MADE - same method as servo_char, see its header for the
//  full worked example. Short version at 333 Hz, 14 bits, pulse 1500 us:
//      duty = 1500 * 333 * 16384 / 10^6 = 8183.6 -> 8184 steps
//
//  HOW THE SLEW LIMIT WORKS - worked example
//  -----------------------------------------
//  Every TICK_US = 10 000 us (10 ms) the program moves each running joint
//  toward its target by at most
//      max_step = RATE * 10 ms = 200 us/s * 0.010 s = 2 us per tick.
//  A 300 us move therefore takes 300 / 2 = 150 ticks = 1.5 s. For the PDI,
//  300 us is about 28 degrees, so the arm swings at about 19 degrees per
//  second: gentle enough to watch, and to stop with OFF.
//
//  Project rules kept even in a throwaway: no delay(), no heap allocation.
// ===========================================================================

#include <Arduino.h>    // Serial, analogWrite, analogWriteFrequency, elapsedMicros
#include <cstdlib>      // strtol(): text -> number
#include <cstring>      // strlen(), strcmp(): C string helpers
#include <strings.h>    // strcasecmp(): compare C strings ignoring upper/lower case

// ---------------------------------------------------------------------------
// Settings that never change while the program runs.
// ---------------------------------------------------------------------------
constexpr uint8_t  PWM_BITS  = 14;               // resolution: 2^14 = 16384 steps per frame
constexpr uint32_t PWM_STEPS = 1u << PWM_BITS;   // 16384 (1 shifted left 14 times)

constexpr uint32_t TICK_US     = 10000;          // slew update period: 10 ms
constexpr float    RATE_MIN    = 20.0f;          // slowest allowed slew speed, us per second
constexpr float    RATE_MAX    = 2000.0f;        // fastest allowed slew speed, us per second
constexpr float    RATE_START  = 200.0f;         // default slew speed, us per second

// ---------------------------------------------------------------------------
// One servo. A plain struct: its data and nothing else. Four copies live in
// g_joints below, in this fixed order: top prox, top dist, down prox, down dist.
// ---------------------------------------------------------------------------
struct Joint {
  const char* arm;        // "top" or "down" - what you type first
  const char* part;       // "prox" (the PDI, makes the tap) or "dist" (the EMAX, picks the site)
  uint8_t     pin;        // Teensy pin carrying the signal (through a 1 kohm resistor)
  uint32_t    hz;         // pulses per second for this joint's PWM timer
  uint16_t    min_us;     // shortest pulse we allow, us
  uint16_t    max_us;     // longest pulse we allow, us
  float       pos_us;     // where the pulse is NOW (fractional, because slewing is slow)
  uint16_t    target_us;  // where the pulse is heading, us
  uint16_t    sent_us;    // the whole-microsecond pulse last handed to the hardware
  bool        on;         // false = no pulses (safe start-up)
};

constexpr size_t N_JOINTS = 4;   // how many entries g_joints has

// PDI limits 600-1830 us. The top, 1830 us, is the measured ZERO: the pulse at
// which the proximal link is level (both arms, 2026-11-24). The arm must never be
// commanded above level, so ZERO is the ceiling. FEWER microseconds move it DOWN.
// The floor, 600 us, is inside the 500-2500 us range measured in Step 3.1, with a
// margin. Narrow it once the arm's real safe swing is known.
// EMAX limits 900-1900 us: the operating range from Step 3.1. The top arm's EMAX
// ceiling is its distal ZERO, 1900 us. The down arm's ceiling is its distal ZERO,
// 1860 us (chosen 2026-11-24), so its start pose is also the highest it may go.
Joint g_joints[N_JOINTS] = {
  //  arm     part    pin  hz   min   max   pos   target sent  on
  {"top",  "prox",  3, 333,  600, 1830, 0.0f, 0, 0, false},   // PDI, pins 2 and 3 share a timer
  {"top",  "dist",  6, 100,  900, 1900, 0.0f, 0, 0, false},   // EMAX, pins 6 and 9 share another
  {"down", "prox",  2, 333,  600, 1830, 0.0f, 0, 0, false},
  {"down", "dist",  9, 100,  900, 1860, 0.0f, 0, 0, false},
};

float g_rate_us_per_s = RATE_START;   // current slew speed, set by the RATE command

elapsedMicros g_since_tick;           // counts up by itself; rollover-safe (handled inside the class)

// Line buffer, the same idea as HostLink: collect characters until '\n'.
constexpr size_t LINE_SIZE = 64;      // 63 characters + the '\0' terminator
char   g_line[LINE_SIZE];             // the characters collected so far
size_t g_len      = 0;                // how many there are
bool   g_overflow = false;            // true if this line was too long

// ---------------------------------------------------------------------------
// duty_for(): turn a pulse length into a number of PWM steps.
// 64-bit arithmetic because the middle of the sum is big (2500*400*16384 does
// not fit in 32 bits). Adding 500 000 before dividing rounds to nearest.
// Example: 1500 us at 333 Hz -> 8183.6 -> 8184 steps.
// ---------------------------------------------------------------------------
uint32_t duty_for(uint32_t pulse_us, uint32_t hz) {
  const uint64_t scaled = (uint64_t)pulse_us * hz * PWM_STEPS;   // us * Hz * steps
  return (uint32_t)((scaled + 500000u) / 1000000u);              // round, -> steps
}

// ---------------------------------------------------------------------------
// real_pulse_us(): the pulse length the servo really gets for a given duty.
// The reverse of duty_for(), without rounding. Example: 8184 steps at 333 Hz
// -> 8184 * 1 000 000 / (333 * 16384) = 1500.0 us.
// ---------------------------------------------------------------------------
float real_pulse_us(uint32_t duty, uint32_t hz) {
  return (float)duty * 1000000.0f / ((float)hz * (float)PWM_STEPS);
}

// ---------------------------------------------------------------------------
// write_pulse(): hand one whole-microsecond pulse to the PWM hardware and
// remember that we did, so update_slew() only writes when the value changes.
// ---------------------------------------------------------------------------
void write_pulse(Joint& j, uint16_t pulse_us) {
  analogWrite(j.pin, duty_for(pulse_us, j.hz));   // the hardware repeats this pulse every frame
  j.sent_us = pulse_us;                           // remember what the hardware now has
}

// ---------------------------------------------------------------------------
// find_joint(): look up a joint by the two words the user typed.
// strcasecmp compares two C strings ignoring case and returns 0 when equal.
// Returns nullptr (a pointer to nothing) if no joint matches.
// ---------------------------------------------------------------------------
Joint* find_joint(const char* arm, const char* part) {
  for (size_t i = 0; i < N_JOINTS; i++) {                          // try each joint in turn
    if (strcasecmp(arm, g_joints[i].arm) == 0 &&                   // arm word matches...
        strcasecmp(part, g_joints[i].part) == 0) {                 // ...and so does the part word
      return &g_joints[i];                                         // address of the matching joint
    }
  }
  return nullptr;                                                  // no match
}

// ---------------------------------------------------------------------------
// parse_value(): read a whole number from text, strictly, and note whether it
// had a leading sign.
//   "1500" -> ok, 1500, absolute
//   "+5"   -> ok, 5,    relative (a leading + or - means "from where it is")
//   "-5"   -> ok, -5,   relative
//   "15x"  -> refused (the 'x' is left over)
// strtol reads digits and sets `end` to the first character it did NOT use; we
// accept only if at least one digit was read and nothing is left after it.
// ---------------------------------------------------------------------------
bool parse_value(const char* text, long& out, bool& relative) {
  relative = (text[0] == '+' || text[0] == '-');       // a leading sign marks a relative jog
  char* end = nullptr;                                  // strtol fills this in
  const long value = strtol(text, &end, 10);            // base 10; understands + and -
  if (end == text || *end != '\0') return false;        // no digits, or junk after them
  out = value;
  return true;
}

// ---------------------------------------------------------------------------
// split(): cut a line into words IN PLACE, without copying and without heap.
// It overwrites each space with '\0' (the C string terminator) so every word
// becomes its own little C string, and records where each word starts.
//   "top prox 1500"  ->  tok[0]="top"  tok[1]="prox"  tok[2]="1500"  returns 3
// Words beyond max_tok are ignored; the caller's word-count checks then fail,
// which is how a too-long line gets rejected.
// ---------------------------------------------------------------------------
size_t split(char* line, char* tok[], size_t max_tok) {
  size_t n = 0;                                         // words found so far
  char* p = line;                                       // read position in the line
  while (*p != '\0' && n < max_tok) {                   // until the end or the word limit
    while (*p == ' ') { *p = '\0'; p++; }               // turn spaces into terminators, skip them
    if (*p == '\0') break;                              // only spaces were left: done
    tok[n] = p;                                         // a word starts here
    n++;                                                // count it
    while (*p != '\0' && *p != ' ') p++;                // walk to the end of the word
  }
  return n;
}

// ---------------------------------------------------------------------------
// set_joint(): the heart of a joint command.
//
//   value, absolute : target = value
//   value, relative : target = current target + value
//
// Relative moves add to the TARGET, not the current position, so typing
// "+5" three times quickly gives +15 even while the joint is still slewing.
// Example: target 1500, "down dist -5" -> target 1495.
//
// A joint that is OFF can only be started with an absolute value: a relative
// move from nowhere makes no sense. The first pulse is applied immediately,
// with no slewing (see SAFETY 1 in the header).
// ---------------------------------------------------------------------------
void set_joint(Joint& j, long value, bool relative) {
  if (relative && !j.on) {                                         // jog from nothing?
    Serial.println("ERR that joint is off; give an absolute pulse first, e.g. top prox 1500");
    return;
  }
  const long wanted = relative ? (long)j.target_us + value : value;   // the pulse we are asking for
  if (wanted < (long)j.min_us || wanted > (long)j.max_us) {        // outside the safe range?
    Serial.print("ERR ");
    Serial.print(j.arm);  Serial.print(' ');  Serial.print(j.part);
    Serial.print(" must stay between ");
    Serial.print(j.min_us);  Serial.print(" and ");  Serial.print(j.max_us);
    Serial.print(" us; asked for ");  Serial.println(wanted);
    return;
  }
  j.target_us = (uint16_t)wanted;                                  // where it should end up
  if (!j.on) {                                                     // first command for this joint
    j.on = true;                                                   // start its pulses
    j.pos_us = (float)wanted;                                      // it starts AT the target: no slew
    write_pulse(j, (uint16_t)wanted);                              // hand the pulse to the hardware
  }
}

// ---------------------------------------------------------------------------
// update_slew(): called every TICK_US from loop(). Moves each running joint a
// little toward its target and writes the new pulse if the whole-microsecond
// value changed.
//
// Worked example, RATE 200, tick 10 ms -> max_step = 2 us.
//   pos 1500.0, target 1600:  diff = +100 -> pos becomes 1502.0
//   pos 1599.0, target 1600:  diff = +1.0 is within max_step -> pos = 1600.0
// At RATE 20 the step is 0.2 us, so pos is fractional and the whole-
// microsecond pulse only changes every fifth tick; that is why pos_us is a
// float and sent_us is not.
// ---------------------------------------------------------------------------
void update_slew() {
  const float max_step = g_rate_us_per_s * ((float)TICK_US / 1000000.0f);   // us per tick
  for (size_t i = 0; i < N_JOINTS; i++) {                          // every joint
    Joint& j = g_joints[i];                                        // a short name for this one
    if (!j.on) continue;                                           // no pulses: nothing to move
    const float diff = (float)j.target_us - j.pos_us;              // how far still to go (signed)
    if (diff > max_step)        j.pos_us += max_step;              // far above: take a full step up
    else if (diff < -max_step)  j.pos_us -= max_step;              // far below: take a full step down
    else                        j.pos_us = (float)j.target_us;     // close: land exactly on target
    const uint16_t pulse = (uint16_t)(j.pos_us + 0.5f);            // round to a whole microsecond
    if (pulse != j.sent_us) write_pulse(j, pulse);                 // only write when it changed
  }
}

// ---------------------------------------------------------------------------
// show(): print every joint, including what the servo REALLY receives.
// Example line:
//   top  prox pin=3 hz=333 on=yes now=1500.0 target=1500 real_us=1500.0 limits=600-2400
// ---------------------------------------------------------------------------
void show() {
  for (size_t i = 0; i < N_JOINTS; i++) {
    const Joint& j = g_joints[i];
    Serial.print(j.arm);   Serial.print(' ');  Serial.print(j.part);
    Serial.print(" pin="); Serial.print(j.pin);
    Serial.print(" hz=");  Serial.print(j.hz);
    Serial.print(" on=");  Serial.print(j.on ? "yes" : "no");
    if (j.on) {                                                    // numbers mean nothing when off
      Serial.print(" now=");     Serial.print(j.pos_us, 1);
      Serial.print(" target=");  Serial.print(j.target_us);
      Serial.print(" real_us="); Serial.print(real_pulse_us(duty_for(j.sent_us, j.hz), j.hz), 1);
    }
    Serial.print(" limits="); Serial.print(j.min_us);
    Serial.print('-');        Serial.println(j.max_us);
  }
  Serial.print("rate_us_per_s="); Serial.println(g_rate_us_per_s, 0);
}

// ---------------------------------------------------------------------------
// mark(): print all four pulses on ONE line, ready to paste into the notes.
// Example: MARK site3_touch top_prox=1480 top_dist=1500 down_prox=off down_dist=off
// Prints sent_us, the pulse the servo is actually getting (not the target).
// ---------------------------------------------------------------------------
void mark(const char* label) {
  Serial.print("MARK ");
  Serial.print(label);
  for (size_t i = 0; i < N_JOINTS; i++) {
    const Joint& j = g_joints[i];
    Serial.print(' ');
    Serial.print(j.arm);  Serial.print('_');  Serial.print(j.part);  Serial.print('=');
    if (j.on) Serial.print(j.sent_us); else Serial.print("off");
  }
  Serial.println();
}

// ---------------------------------------------------------------------------
// switch_off(): stop one joint's pulses. 0 steps HIGH means the wire stays LOW.
// ---------------------------------------------------------------------------
void switch_off(Joint& j) {
  analogWrite(j.pin, 0);    // no pulses; the servo gets nothing to follow
  j.on = false;             // and our own bookkeeping agrees
}

// ---------------------------------------------------------------------------
// handle_line(): act on one complete command. The line is split into words
// first; the first word decides what kind of command it is.
// ---------------------------------------------------------------------------
void handle_line(char* line) {
  char* tok[6];                                          // pointers to the words
  const size_t n = split(line, tok, 6);                  // cut the line into words
  if (n == 0) return;                                    // nothing but spaces

  if (strcasecmp(tok[0], "SHOW") == 0 && n == 1) {       // SHOW
    show();
    return;
  }

  if (strcasecmp(tok[0], "OFF") == 0 && (n == 1 || n == 3)) {   // OFF   or   OFF <arm> <part>
    if (n == 1) {                                        // no joint named: everything
      for (size_t i = 0; i < N_JOINTS; i++) switch_off(g_joints[i]);
    } else {                                             // one joint named
      Joint* j = find_joint(tok[1], tok[2]);
      if (j == nullptr) { Serial.println("ERR unknown joint; arm is top or down, part is prox or dist"); return; }
      switch_off(*j);
    }
    show();
    return;
  }

  if (strcasecmp(tok[0], "RATE") == 0 && n == 2) {       // RATE <us per second>
    long value = 0;
    bool relative = false;
    if (!parse_value(tok[1], value, relative) || relative ||
        value < (long)RATE_MIN || value > (long)RATE_MAX) {
      Serial.println("ERR RATE needs a number from 20 to 2000, e.g. RATE 200");
      return;
    }
    g_rate_us_per_s = (float)value;                      // takes effect on the next tick
    show();
    return;
  }

  if (strcasecmp(tok[0], "MARK") == 0 && n == 2) {       // MARK <label>
    mark(tok[1]);
    return;
  }

  if (n == 3) {                                          // <arm> <part> <value>
    Joint* j = find_joint(tok[0], tok[1]);               // which servo?
    long value = 0;
    bool relative = false;
    if (j != nullptr && parse_value(tok[2], value, relative)) {
      set_joint(*j, value, relative);                    // do it (or refuse, with a reason)
      show();                                            // and show the result
      return;
    }
  }

  Serial.println("ERR could not understand that. Commands:");   // anything else
  Serial.println("    top|down prox|dist <us>   e.g. top prox 1500");
  Serial.println("    top|down prox|dist +<n>   or -<n>, a relative jog");
  Serial.println("    RATE <n>, MARK <label>, SHOW, OFF, OFF <arm> <part>");
}

// ---------------------------------------------------------------------------
// setup(): runs once at power-up. Sets each pin's frequency and leaves every
// joint with NO pulses, so nothing moves until you type a command.
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);                      // USB serial; the number is nominal
  analogWriteResolution(PWM_BITS);           // 14 bits for every PWM pin
  for (size_t i = 0; i < N_JOINTS; i++) {
    Joint& j = g_joints[i];
    pinMode(j.pin, OUTPUT);                  // the pin drives the signal wire
    analogWriteFrequency(j.pin, (float)j.hz);   // this pin's timer rate (shared with its partner pin)
    analogWrite(j.pin, 0);                   // 0 steps HIGH: the wire stays LOW, no pulses
  }
  g_since_tick = 0;                          // start the slew clock now
}

// ---------------------------------------------------------------------------
// loop(): runs forever. Collect characters into lines without ever waiting,
// exactly like HostLink::poll(), and run the slew update once per tick.
// ---------------------------------------------------------------------------
void loop() {
  while (Serial.available() > 0) {           // only what has already arrived
    const char c = (char)Serial.read();

    if (c == '\n') {                         // end of a command
      if (g_overflow) {
        Serial.println("ERR line too long");
      } else if (g_len > 0) {                // ignore empty lines
        g_line[g_len] = '\0';                // terminate the C string
        handle_line(g_line);
      }
      g_len = 0;                             // start the next line fresh
      g_overflow = false;
    } else if (c == '\r') {
      // Windows sends \r\n; throw the \r away.
    } else if (g_len < LINE_SIZE - 1) {      // room left (one slot kept for '\0')
      g_line[g_len] = c;
      g_len = g_len + 1;
    } else {
      g_overflow = true;                     // full: drop it, report at '\n'
    }
  }

  if (g_since_tick >= TICK_US) {             // has a tick period passed? (elapsedMicros is rollover-safe)
    g_since_tick -= TICK_US;                 // subtract one period, keeping the rhythm steady
    update_slew();                           // move every running joint a little
  }
}
