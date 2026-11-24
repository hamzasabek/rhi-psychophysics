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
//    KEYS              single-key mode, no Enter needed (see KEY MODE below)
//
//  KEY MODE (type KEYS; use 'pio device monitor', which sends each key at once)
//  ---------------------------------------------------------------------------
//    1 / 2    select the top arm / the down arm
//    w / s    selected proximal UP / DOWN by one step (down = fewer us)
//    a / d    selected distal fewer us / more us by one step
//    z / x    smaller / larger step: 5, 10, 30 or 100 us (starts at 30)
//    u        lift the selected proximal to its ZERO (its ceiling)
//    b        go back down to the pulse it held before u, approaching from above
//    m, 0-5   print a MARK line: 0 = raised, 1-5 = site 1 to 5 (once nothing moves)
//    Space    lift BOTH proximals to ZERO now
//    ?        show these keys again           q   leave key mode
//    Keys are lowercase. Arrow keys and other escape sequences are ignored.
//    Everything uses the same limits and the same RATE slew as typed commands.
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
// KEY MODE - single keys move the joints with no Enter, for finding the sites
// by hand.
//
// The loop it is built for, per arm and per site:
//   1. aim the distal (a / d) until the tip is over the site;
//   2. lower the proximal (s) until the tip touches, raise it (w) if too far;
//   3. lift with u, come back down with b: the tip should land on the same spot.
//      This also tests the descent from above, the direction the real tap travels;
//   4. when the tip touches exactly, press m then the site number: the MARK line
//      holds BOTH pulses (distal = AIM, proximal = TOUCH).
//
// Key mode adds no new way to move a servo: every key ends in set_joint(), so the
// limits and the RATE slew limit apply exactly as for typed commands. The only
// extra rule: a joint that is OFF is never started from a key.
// ---------------------------------------------------------------------------
constexpr uint16_t STEP_US[]   = {5, 10, 30, 100};   // the step sizes z and x choose between, in us
constexpr size_t   N_STEPS     = sizeof(STEP_US) / sizeof(STEP_US[0]);   // how many sizes: 4
constexpr size_t   STEP_START  = 2;                  // index 2 = 30 us, the smallest step seen on the arm

// What 'm' followed by 0-5 prints as the MARK label. Index = the digit typed.
const char* const POSE_LABELS[6] = {"raised", "site1", "site2", "site3", "site4", "site5"};

bool     g_key_mode     = false;                     // true while single keys act at once
uint8_t  g_sel_arm      = 0;                         // selected arm: 0 = top, 1 = down
size_t   g_step_index   = STEP_START;                // which entry of STEP_US is in use
bool     g_wait_site    = false;                     // true right after 'm': the next key names the pose
uint8_t  g_esc          = 0;                         // 0 normal, 1 just saw ESC, 2 inside an ESC [ sequence
uint16_t g_resume_us[2] = {0, 0};                    // per arm: the proximal pulse held before 'u'
bool     g_resume_ok[2] = {false, false};            // per arm: is that remembered pulse valid?

// ---------------------------------------------------------------------------
// sel_prox(), sel_dist(): the selected arm's two joints. g_joints is ordered
// top prox, top dist, down prox, down dist, so arm 0 owns indices 0 and 1 and
// arm 1 owns 2 and 3: proximal = 2 * arm, distal = 2 * arm + 1.
// ---------------------------------------------------------------------------
Joint& sel_prox() { return g_joints[2 * g_sel_arm]; }        // e.g. arm 1 -> index 2, the down proximal
Joint& sel_dist() { return g_joints[2 * g_sel_arm + 1]; }    // e.g. arm 1 -> index 3, the down distal

// ---------------------------------------------------------------------------
// any_moving(): true while any running joint has not yet reached its target.
// sent_us is what the servo is getting, target_us where it is heading; they
// differ for as long as the slew is still working.
// ---------------------------------------------------------------------------
bool any_moving() {
  for (size_t i = 0; i < N_JOINTS; i++) {                    // every joint
    const Joint& j = g_joints[i];                            // a short name for this one
    if (j.on && j.sent_us != j.target_us) return true;       // still travelling
  }
  return false;                                              // all settled
}

// ---------------------------------------------------------------------------
// nudge(): move one joint by `delta` microseconds from where it is heading, and
// stop AT a limit instead of refusing (a typed command refuses; with a key,
// "as far as allowed" is what you mean).
// Example: top prox target 1810, ceiling 1830, key w with step 30:
//   wanted = 1810 + 30 = 1840, above 1830, so it is clamped to 1830 and we say so.
// The move itself is done by set_joint(), which only sets the target; the slew
// in update_slew() then walks there at RATE.
// ---------------------------------------------------------------------------
void nudge(Joint& j, long delta) {
  if (!j.on) {                                               // joint has no pulses yet?
    Serial.println("ERR that joint is off; leave key mode (q) and send its start pulse first");
    return;                                                  // never start a joint from a key
  }
  long wanted = (long)j.target_us + delta;                   // where the key asks to go
  bool clamped = false;                                      // did we hit a limit?
  if (wanted > (long)j.max_us) { wanted = (long)j.max_us; clamped = true; }   // not above the ceiling
  if (wanted < (long)j.min_us) { wanted = (long)j.min_us; clamped = true; }   // not below the floor
  set_joint(j, wanted, false);                               // absolute target; always inside the limits now
  if (clamped) Serial.println("at limit");                   // tell the user the key was cut short
}

// ---------------------------------------------------------------------------
// lift_arm(): send one arm's proximal up to its ZERO, which is its ceiling
// (max_us), remembering where it was so that 'b' can come back.
// The pulse is remembered only if it is BELOW ZERO: pressing u twice must not
// overwrite the remembered touch pulse with ZERO itself.
// ---------------------------------------------------------------------------
void lift_arm(uint8_t arm) {
  Joint& p = g_joints[2 * arm];                              // that arm's proximal
  if (!p.on) return;                                         // nothing running: nothing to lift
  if (p.target_us < p.max_us) {                              // currently below ZERO?
    g_resume_us[arm] = p.target_us;                          // remember this pulse
    g_resume_ok[arm] = true;                                 // and that it is valid
  }
  p.target_us = p.max_us;                                    // head for ZERO (slewed, so no snap)
}

// ---------------------------------------------------------------------------
// resume_arm(): go back down to the pulse remembered by lift_arm(). The slew
// walks DOWN from ZERO, so the tip always arrives from above, like a real tap.
// ---------------------------------------------------------------------------
void resume_arm(uint8_t arm) {
  Joint& p = g_joints[2 * arm];                              // that arm's proximal
  if (!p.on || !g_resume_ok[arm]) {                          // nothing remembered?
    Serial.println("nothing to go back to: press u first");
    return;
  }
  p.target_us = g_resume_us[arm];                            // head for the remembered pulse
}

// ---------------------------------------------------------------------------
// key_report(): one line giving the selected arm, where its two joints are
// heading, and the step size. Example: top  prox=1712  dist=1650  step=30us
// ---------------------------------------------------------------------------
void key_report() {
  const Joint& p = sel_prox();                               // the selected arm's proximal
  const Joint& d = sel_dist();                               // and its distal
  Serial.print(p.arm);                                       // "top" or "down"
  Serial.print("  prox=");
  if (p.on) Serial.print(p.target_us); else Serial.print("off");   // the pulse it is heading for
  Serial.print("  dist=");
  if (d.on) Serial.print(d.target_us); else Serial.print("off");
  Serial.print("  step=");
  Serial.print(STEP_US[g_step_index]);                       // current step size
  Serial.println("us");
}

// ---------------------------------------------------------------------------
// key_help(): print the key table (the same one as in the header comment).
// ---------------------------------------------------------------------------
void key_help() {
  Serial.println("KEY MODE  (no Enter needed; keys are lowercase)");
  Serial.println("  1 / 2   select top arm / down arm");
  Serial.println("  w / s   proximal up / down by one step (down = fewer us)");
  Serial.println("  a / d   distal fewer us / more us by one step");
  Serial.println("  z / x   smaller / larger step: 5, 10, 30, 100 us");
  Serial.println("  u       lift selected proximal to its ZERO");
  Serial.println("  b       go back down to where it was before u, from above");
  Serial.println("  m 0-5   MARK line: 0 = raised, 1-5 = site 1 to 5 (once nothing moves)");
  Serial.println("  Space   lift BOTH proximals to ZERO now");
  Serial.println("  ?  help     q  leave key mode");
}

// ---------------------------------------------------------------------------
// handle_key(): act on ONE character while key mode is on.
//
// Arrow keys: pio's monitor sends an arrow as the three characters ESC [ B (or
// A, C, D). Read as ordinary keys the B would act as 'b' and send an arm down
// toward the hand. So ESC starts a small state machine (g_esc) that swallows
// the whole sequence: after ESC, a '[' opens a sequence, and the sequence ends
// at its final byte, any character from '@' (0x40) to '~' (0x7E). The digits
// and ';' inside a sequence are below 0x40, so they are swallowed too.
// ---------------------------------------------------------------------------
void handle_key(char c) {
  if (g_esc == 1) {                                          // the character right after ESC
    g_esc = (c == '[') ? 2 : 0;                              // '[' opens a sequence; anything else ends it
    return;                                                  // either way, swallow this character
  }
  if (g_esc == 2) {                                          // inside ESC [ ...
    if (c >= '@' && c <= '~') g_esc = 0;                     // a final byte ends the sequence
    return;                                                  // swallow every character of it
  }
  if (c == 27) { g_esc = 1; return; }                        // ESC itself (code 27): start swallowing
  if (c == '\r' || c == '\n') return;                        // Enter does nothing in key mode

  if (g_wait_site) {                                         // 'm' was pressed: this key names the pose
    g_wait_site = false;                                     // one key only
    if (c < '0' || c > '5') {                                // not a valid pose digit?
      Serial.println("mark cancelled: press m, then 0 (raised) or 1-5 (site)");
    } else if (any_moving()) {                               // a joint is still travelling?
      Serial.println("still moving: wait until it stops, then press m again");
    } else {
      mark(POSE_LABELS[c - '0']);                            // e.g. '3' - '0' = 3 -> "site3"
    }
    return;
  }

  bool report = true;                                        // most keys end by printing the state
  switch (c) {                                               // which key?
    case '1': g_sel_arm = 0; break;                          // select the top arm
    case '2': g_sel_arm = 1; break;                          // select the down arm
    case 'w': nudge(sel_prox(),  (long)STEP_US[g_step_index]); break;   // proximal UP: more us (1760 + 30 -> 1790)
    case 's': nudge(sel_prox(), -(long)STEP_US[g_step_index]); break;   // proximal DOWN: fewer us (1760 - 30 -> 1730)
    case 'a': nudge(sel_dist(), -(long)STEP_US[g_step_index]); break;   // distal: fewer us
    case 'd': nudge(sel_dist(),  (long)STEP_US[g_step_index]); break;   // distal: more us
    case 'z': if (g_step_index > 0) g_step_index--; break;              // a smaller step, if there is one
    case 'x': if (g_step_index + 1 < N_STEPS) g_step_index++; break;    // a larger step, if there is one
    case 'u': lift_arm(g_sel_arm); break;                    // lift the selected proximal to ZERO
    case 'b': resume_arm(g_sel_arm); break;                  // come back down from above
    case ' ':                                                // Space: lift BOTH arms
      lift_arm(0);                                           // the top arm
      lift_arm(1);                                           // the down arm
      Serial.println("lifting both proximals to ZERO");
      break;
    case 'm':                                                // start a MARK: the next key is the pose
      g_wait_site = true;
      Serial.println("mark: press 0 (raised) or 1-5 (site)");
      report = false;                                        // nothing to report yet
      break;
    case '?': key_help(); break;                             // show the keys again
    case 'q':                                                // leave key mode
      g_key_mode = false;
      Serial.println("key mode off; typed commands work again");
      show();                                                // print every joint, as typed commands do
      report = false;
      break;
    default: report = false; break;                          // any other key: ignore silently
  }
  if (report) key_report();                                  // show where the selected arm is heading
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

  if (strcasecmp(tok[0], "KEYS") == 0 && n == 1) {       // KEYS: switch to single-key mode
    g_key_mode  = true;                                  // from now on loop() sends characters to handle_key()
    g_wait_site = false;                                 // no half-finished MARK
    g_esc       = 0;                                     // no half-read escape sequence
    key_help();                                          // show the keys
    key_report();                                        // and where the selected arm is
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
  Serial.println("    RATE <n>, MARK <label>, SHOW, OFF, OFF <arm> <part>, KEYS");
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

    if (g_key_mode) {                        // key mode: every character acts at once, no Enter
      handle_key(c);
      continue;                              // next character; skip the line collector below
    }

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
