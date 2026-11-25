// ===========================================================================
//  arm_taps - bench experiment: try the tap stimulation by hand
//
//  WHAT IT DOES
//  ------------
//  Runs the tap stimulation on the two arms with the five sites TAUGHT by the
//  site-calibration procedure (docs/site-calibration.md), read from
//  firmware/experiments/common/calibration.h. You can WATCH different
//  stimulations: which site, how big the
//  asynchrony, how hard the press. It is a preview of Step 3.5 (the three-stage
//  tap) and of the asynchrony work in Group 5. It is NOT the real firmware and
//  it MEASURES NOTHING: there is no contact sensor yet (Step 3.4), so every time
//  printed is a COMMANDED time, not an achieved one.
//
//  THE ARMS (decided 2026-11-24)
//  -----------------------------
//    "down" arm = REAL hand   (arm index 0)   PDI on pin 2, EMAX on pin 9
//    "top"  arm = RUBBER hand (arm index 1)   PDI on pin 3, EMAX on pin 6
//
//  SIGN CONVENTION (PROJECT.md 1.5):   t_rubber = t_real + delta
//  A POSITIVE delta means the REAL hand (the down arm) is touched FIRST.
//  A negative delta means the rubber hand (the top arm) is touched first.
//  The delta applies to the planned moment of CONTACT, which here is the end of
//  the downward stroke.
//
//  THE SITE TABLE (calibration.h, docs/site-calibration.md)
//  --------------------------------------------------------
//  Per hand and per site, two TAUGHT pulses:
//    AIM_i     distal pulse that puts the tube above site i
//    CONTACT_i proximal pulse at which the tube first touches site i
//  and two constants, the same for every participant (fewer us = lower):
//    RAISED_i = CONTACT_i + S      S = START (default calib::START_US = 100 us)
//    TOUCH_i  = CONTACT_i - P      P = PRESS (default calib::PRESS_US = 10 us)
//  Example: CONTACT 1610 -> RAISED 1710, TOUCH 1600: a 110 us stroke, the same
//  at every site, so every tap is the same movement with the same delay.
//
//  THE TAP (PROJECT.md 2.2): three stages, per arm, per tap
//  --------------------------------------------------------
//    A  SELECT   in the gap between taps; slow, not timing-critical; three parts:
//                (1) the proximal RISES to the TRAVEL height: the arm's highest
//                    RAISED_i plus TRAVEL, so it clears the finger at EVERY site;
//                (2) the distal slews to the site's AIM pulse, with the arm well
//                    clear of the finger;
//                (3) the proximal comes back DOWN to that site's RAISED_i.
//    B  TAP      the proximal joint goes DOWN from RAISED_i to TOUCH_i on a
//                minimum-jerk curve (see min_jerk()), then holds.
//    C  LIFT     the proximal joint goes back UP to RAISED_i, and stage A then
//                rises on to TRAVEL.
//  The distal joint moves ONLY while the proximal is up at the TRAVEL height.
//  The tap stroke (B) is always RAISED_i to TOUCH_i (S + P), whatever TRAVEL is.
//
//  COMMANDS (type one, press Enter; upper or lower case)
//  -----------------------------------------------------
//    HOME           send the start pulses (both proximals 1830, distals 1900/1860).
//                   FIRST time: the pulses start at once, so switch the SERVO
//                   POWER ON only after this command (pulses first, power second).
//                   Later: slews both arms back to those start poses.
//    ONE <1-5>      one tap on that site, both arms together (asynchrony applies)
//    RAND           one tap on a random site
//    TRIAL          N_TAPS (16) taps per arm, 2 s apart, random sites with no
//                   immediate repeat and all five sites used, the same site for
//                   both arms
//    ASYN <ms>      asynchrony delta in ms, -1000 to 1000 (positive = real first)
//    ASYN RANDOM    pick one of 0, +-150, +-300, +-500 ms for EACH run, and keep
//                   it hidden until the run is DONE, so you can judge blind
//    HOLD <ms>      how long the tip stays down, 20 to 1000 (default 120)
//    PRESS <us>     P: how far past CONTACT the tap is commanded, 0 to 60
//                   (default calib::PRESS_US); the same for both hands
//    START <us>     S: how far above CONTACT the tap starts, 30 to 200
//                   (default calib::START_US); the same for both hands
//    STROKE <ms>    duration of the downward stroke, 20 to 500 (default 50)
//    TRAVEL <us>    how far ABOVE the highest RAISED_i the arm goes between
//                   touches, 0 to 200 (default 100, about 9 degrees). An arm
//                   never goes above level, so SHOW prints the height each arm
//                   really uses.
//    SHOW           print everything
//    STOP           (or just '!', or a space at the start of an empty line, no
//                   Enter needed) stop the run and lift both arms to ZERO
//
//  SAFETY
//  ------
//  1. Nothing is sent at power-up. HOME starts the pulses.
//  2. Every pulse written is clamped to its joint's limits: the proximals never
//     go above 1830 us (level) and the distals never above their start pulse.
//  3. A run is refused if either hand's table has a site not taught (a 0), or
//     a RAISED_i, TOUCH_i or AIM_i outside its joint's limits.
//  4. Uploading this resets the board and the arms go limp: support them and
//     switch the servo power OFF first.
//
//  Project rules kept even in a throwaway: no delay(), no heap allocation,
//  and no timestamp is ever compared directly (clk::is_due() is used).
// ===========================================================================

#include <Arduino.h> // Serial, analogWrite, analogWriteFrequency, random, micros
#include <cstdlib>   // strtol(): text -> number
#include <strings.h> // strcasecmp(): compare C strings ignoring upper/lower case

#include "Clock.h"       // the real firmware's clk::now_us() and clk::is_due() (see platformio.ini)
#include "calibration.h" // the taught site tables, P and S (firmware/experiments/common)

// ---------------------------------------------------------------------------
// Settings that never change while the program runs.
// ---------------------------------------------------------------------------
constexpr uint8_t PWM_BITS = 14;               // resolution: 2^14 = 16384 steps per frame
constexpr uint32_t PWM_STEPS = 1u << PWM_BITS; // 16384 (1 shifted left 14 times)
constexpr uint32_t PROX_HZ = 333;              // PDI refresh rate chosen in Step 3.1
constexpr uint32_t DIST_HZ = 100;              // EMAX refresh rate chosen in Step 3.1

constexpr size_t N_ARMS = 2;  // two arms
constexpr size_t REAL = 0;    // index of the arm that taps the REAL hand (down arm)
constexpr size_t RUBBER = 1;  // index of the arm that taps the RUBBER hand (top arm)
constexpr size_t N_SITES = calib::N_SITES; // five sites per finger (PROJECT.md 1.4), from calibration.h
constexpr size_t N_TAPS = 16; // taps per arm in one TRIAL (PROJECT.md 1.4)

constexpr uint32_t PERIOD_US = 2000000;   // 2000 ms between taps of one arm (PROJECT.md 1.4)
constexpr uint32_t LEAD_US = 2000000;     // 2 s from the command to the first contact, for stage A (rise, aim, lower)
constexpr uint16_t TRAVEL_START_US = 100; // starting extra height above the highest RAISED between touches: about 9 degrees
constexpr uint32_t LIFT_US = 50000;       // duration of the upward stroke: 50 ms

constexpr float PROX_RATE = 600.0f;     // slew speed of the proximal when NOT tapping, us per second
constexpr float DIST_RATE = 1000.0f;    // slew speed of the distal (stage A), us per second
constexpr uint32_t SLEW_TICK_US = 1000; // slewing is updated every 1 ms

constexpr size_t LINE_SIZE = 64; // longest typed line: 63 characters + the '\0' terminator

// The asynchrony levels that ASYN RANDOM picks from (PROJECT.md 1.5, ownership task), in ms.
constexpr int32_t LEVELS_MS[] = {-500, -300, -150, 0, 150, 300, 500};
constexpr size_t N_LEVELS = sizeof(LEVELS_MS) / sizeof(LEVELS_MS[0]); // 7 levels

// ---------------------------------------------------------------------------
// ArmSpec: everything FIXED about one arm. Pulses are in microseconds (Step 3.2,
// docs/geometry.md). "ZERO" is the start pose and also the ceiling: the
// proximal is level at 1830 us; the distal start pulse differs per arm. The
// site table (AIM and CONTACT per site) is the hand's taught table in
// calibration.h; `sites` points at it.
// ---------------------------------------------------------------------------
struct ArmSpec
{
  const char *name;               // what you type: "down" or "top"
  const char *hand;               // "real" or "rubber"
  uint8_t pin_prox;               // Teensy pin of the proximal PDI (through 1 kohm)
  uint8_t pin_dist;               // Teensy pin of the distal EMAX (through 1 kohm)
  uint16_t prox_zero;             // proximal ZERO = level = the highest pulse allowed
  uint16_t prox_min;              // proximal floor: the lowest pulse allowed
  uint16_t dist_zero;             // distal start pulse = the highest pulse allowed
  uint16_t dist_min;              // distal floor: the lowest pulse allowed
  const calib::HandSites *sites;  // this hand's taught AIM and CONTACT pulses (calibration.h)
};

// Index 0 = REAL hand = down arm (the participant's table); index 1 = RUBBER
// hand = top arm (the rubber hand's table).
constexpr ArmSpec SPECS[N_ARMS] = {
    //  name    hand      prox dist  pZero pMin dZero dMin  taught table
    {"down", "real", 2, 9, 1830, 600, 1860, 900, &calib::PARTICIPANT},
    {"top", "rubber", 3, 6, 1830, 600, 1900, 900, &calib::RUBBER},
};

// ---------------------------------------------------------------------------
// The stages one arm passes through. An enum class means the compiler stops us
// mixing these up with plain numbers. State::Off is first, so it is the value 0:
// a zero-filled arm starts Off, with no pulses.
// ---------------------------------------------------------------------------
enum class State : uint8_t
{
  Off,        // no pulses at all (power-up state)
  Idle,       // pulses running; the joints slew toward their targets; nothing scheduled
  Rising,     // stage A, part 1: proximal rising to the TRAVEL height (distal does NOT move yet)
  Selecting,  // stage A, part 2: distal slewing to the site's AIM pulse, proximal up at TRAVEL
  Lowering,   // stage A, part 3: proximal coming back down to RAISED
  Armed,      // aimed, at RAISED, and waiting for the moment the downward stroke must start
  Descending, // stage B: proximal going down on the minimum-jerk curve (FAST, timing-critical)
  Holding,    // tip down; waiting out the hold time
  Lifting     // stage C: proximal going back up to RAISED
};

// Names for printing, in the same order as the enum above.
const char *const STATE_NAMES[] = {"off", "idle", "rising", "selecting", "lowering", "armed", "descending", "holding", "lifting"};

// ---------------------------------------------------------------------------
// ArmRun: everything that CHANGES while the program runs, one per arm. As a
// global it is zero-filled at start-up, so every arm starts Off.
// ---------------------------------------------------------------------------
struct ArmRun
{
  State state;                      // where this arm is in its sequence
  float prox_pos;                  // the proximal pulse being sent NOW (fractional while slewing)
  float dist_pos;                   // the distal pulse being sent NOW
  uint16_t prox_target;             // where a slewing proximal is heading
  uint16_t dist_target;             // where a slewing distal is heading
  uint16_t prox_sent;               // the whole-microsecond pulse last handed to the hardware
  uint16_t dist_sent;               // same for the distal
  uint32_t t_stage_us;              // when the current stroke or hold began
  uint16_t stroke_from;             // pulse at the start of the current stroke
  uint16_t stroke_to;               // pulse at the end of the current stroke
  uint8_t k;                        // index of the tap being worked on (0-based)
  uint8_t n;                        // how many taps this run has
  uint8_t plan_site[N_TAPS];        // site of each tap (0-based)
  uint32_t plan_contact_us[N_TAPS]; // planned moment of contact of each tap, on the micros() clock
};

ArmRun g_run[N_ARMS]; // the run-time state of both arms

// Settings you can change from the keyboard.
uint32_t g_stroke_us = 50000;           // duration of the downward stroke (STROKE), us
uint32_t g_hold_us = 120000;            // how long the tip stays down (HOLD), us; 120 ms is U2 in PROJECT.md
uint16_t g_press_us = calib::PRESS_US;  // P: commanded press past CONTACT (PRESS), us
uint16_t g_start_us = calib::START_US;  // S: start height above CONTACT (START), us
uint16_t g_travel_us = TRAVEL_START_US; // extra height above the highest RAISED between touches (TRAVEL), us
int32_t g_asyn_ms = 0;                  // asynchrony delta (ASYN), ms; positive = real hand first
bool g_asyn_random = false;             // true: pick a level at random for every run (ASYN RANDOM)

// Bookkeeping for the run in progress.
bool g_run_active = false;     // true from the start of a run until DONE or STOP
uint32_t g_trial_start_us = 0; // when the run was commanded; printed times are relative to it
int32_t g_run_delta_ms = 0;    // the delta used by the run in progress
bool g_run_hidden = false;     // true: do not reveal the delta until DONE
uint32_t g_run_id = 0;         // counts runs, for the printed header
uint32_t g_last_slew_us = 0;   // when the slewing was last updated

// Line buffer, the same idea as HostLink: collect characters until '\n'.
char g_line[LINE_SIZE];  // the characters collected so far
size_t g_len = 0;        // how many there are
bool g_overflow = false; // true if this line was too long

// ---------------------------------------------------------------------------
// duty_for(): turn a pulse length into a number of PWM steps (same method as
// servo_char and arm_jog). 64-bit arithmetic because the middle of the sum is
// big. Adding 500 000 before dividing rounds to nearest.
// Worked example: 1830 us at 333 Hz, 14 bits:
//   1830 * 333 * 16384 / 1 000 000 = 9984.2  ->  9984 steps.
// ---------------------------------------------------------------------------
uint32_t duty_for(uint32_t pulse_us, uint32_t hz)
{
  const uint64_t scaled = (uint64_t)pulse_us * hz * PWM_STEPS; // us * Hz * steps
  return (uint32_t)((scaled + 500000u) / 1000000u);            // round, -> steps
}

// ---------------------------------------------------------------------------
// write_prox(): send a proximal pulse, clamped to the arm's limits. The
// ceiling is ZERO (level): no matter what any other code asks for, the arm is
// never driven above level. Remembers the position and only touches the
// hardware when the whole-microsecond pulse actually changes.
// Example: asked for 1840.0 on an arm with ZERO 1830 -> 1830.0 is sent.
// ---------------------------------------------------------------------------
void write_prox(size_t i, float pos)
{
  const ArmSpec &s = SPECS[i]; // this arm's fixed limits
  ArmRun &r = g_run[i];        // and its run-time state
  float p = pos;               // work on a copy
  if (p > (float)s.prox_zero)
    p = (float)s.prox_zero; // never above level
  if (p < (float)s.prox_min)
    p = (float)s.prox_min;                     // never below the floor
  r.prox_pos = p;                              // remember what we are sending
  const uint16_t pulse = (uint16_t)(p + 0.5f); // round to a whole microsecond
  if (pulse != r.prox_sent)
  {                                                    // changed since the last write?
    analogWrite(s.pin_prox, duty_for(pulse, PROX_HZ)); // hand it to the PWM hardware
    r.prox_sent = pulse;                               // remember it
  }
}

// ---------------------------------------------------------------------------
// write_dist(): the same for the distal joint, with its own limits.
// ---------------------------------------------------------------------------
void write_dist(size_t i, float pos)
{
  const ArmSpec &s = SPECS[i]; // this arm's fixed limits
  ArmRun &r = g_run[i];        // and its run-time state
  float p = pos;               // work on a copy
  if (p > (float)s.dist_zero)
    p = (float)s.dist_zero; // never above the start pulse
  if (p < (float)s.dist_min)
    p = (float)s.dist_min;                     // never below the floor
  r.dist_pos = p;                              // remember what we are sending
  const uint16_t pulse = (uint16_t)(p + 0.5f); // round to a whole microsecond
  if (pulse != r.dist_sent)
  {                                                    // changed since the last write?
    analogWrite(s.pin_dist, duty_for(pulse, DIST_HZ)); // hand it to the PWM hardware
    r.dist_sent = pulse;                               // remember it
  }
}

// ---------------------------------------------------------------------------
// slew_toward(): move `pos` toward `target` by at most rate * dt, so a joint
// can never be asked for a jump. dt is the time since the last update, in s.
// Worked example: rate 600 us/s, dt 1 ms -> at most 0.6 us per update.
//   pos 1830.0, target 1650: diff = -180, below -0.6 -> pos becomes 1829.4
//   pos 1650.4, target 1650: diff = -0.4, within 0.6  -> pos becomes 1650.0
// With dt = 0 (no slew tick this pass) nothing moves.
// ---------------------------------------------------------------------------
float slew_toward(float pos, float target, float rate, float dt_s)
{
  const float max_step = rate * dt_s; // the biggest move allowed this update
  const float diff = target - pos;    // how far still to go (signed)
  if (diff > max_step)
    return pos + max_step; // far above: a full step up
  if (diff < -max_step)
    return pos - max_step; // far below: a full step down
  return target;           // close: land exactly on the target
}

// ---------------------------------------------------------------------------
// min_jerk(): the shape of every stroke. s(tau) = 10 tau^3 - 15 tau^4 + 6 tau^5
// for tau from 0 to 1, written below as tau^3 * (10 - 15 tau + 6 tau^2).
// It starts and ends with zero speed AND zero acceleration, so the servo is
// never asked for a jump (Step 3.5 explains why a plain step would be worse).
// Worked examples:  tau = 0    -> 0
//                   tau = 0.5  -> 0.125 * (10 - 7.5 + 1.5) = 0.125 * 4 = 0.5
//                   tau = 1    -> 1
// Outside 0..1 it is clamped, so a late call cannot overshoot.
// ---------------------------------------------------------------------------
float min_jerk(float tau)
{
  if (tau <= 0.0f)
    return 0.0f; // before the start
  if (tau >= 1.0f)
    return 1.0f;                                        // after the end
  const float t3 = tau * tau * tau;                     // tau cubed
  return t3 * (10.0f - 15.0f * tau + 6.0f * tau * tau); // 10 tau^3 - 15 tau^4 + 6 tau^5
}

// ---------------------------------------------------------------------------
// raised_pulse(): where the tap on one site STARTS: RAISED_i = CONTACT_i + S.
// More us = higher, so adding S lifts the tube above the skin. Never above
// level (the arm's ZERO); table_ok() refuses a run that would need that.
// Example: CONTACT 1610, START 100 -> 1710 (about 3 cm above the skin).
// ---------------------------------------------------------------------------
uint16_t raised_pulse(size_t i, uint8_t site)
{
  const ArmSpec &s = SPECS[i];                                  // this arm's limits and table
  long p = (long)s.sites->contact[site] + (long)g_start_us;     // CONTACT plus S
  if (p > (long)s.prox_zero)
    p = (long)s.prox_zero; // never above level
  return (uint16_t)p;      // as a pulse
}

// ---------------------------------------------------------------------------
// touch_pulse(): where the tap on one site ENDS: TOUCH_i = CONTACT_i - P.
// Fewer us = lower, so subtracting P presses past the skin. Never below the
// floor; table_ok() refuses a run that would need that.
// Example: CONTACT 1610, PRESS 10 -> 1600 (about 3 mm past first contact).
// ---------------------------------------------------------------------------
uint16_t touch_pulse(size_t i, uint8_t site)
{
  const ArmSpec &s = SPECS[i];                                  // this arm's limits and table
  long p = (long)s.sites->contact[site] - (long)g_press_us;     // CONTACT minus P
  if (p < (long)s.prox_min)
    p = (long)s.prox_min; // never below the floor
  return (uint16_t)p;     // as a pulse
}

// ---------------------------------------------------------------------------
// travel_pulse(): the height the proximal goes to between touches, while the
// distal swings from one site to the next. The distal sweeps over EVERY site on
// the way, so the height is the arm's HIGHEST RAISED_i plus the TRAVEL extra,
// but never above level (the arm's ZERO).
// Example: RAISED_i = 1720, 1710, 1690, 1690, 1680 and TRAVEL 100
//          -> 1720 + 100 = 1820.  With TRAVEL 200 -> 1920, cut to 1830 (level).
// A higher pulse means a higher arm; 100 us is about 9 degrees.
// ---------------------------------------------------------------------------
uint16_t travel_pulse(size_t i)
{
  const ArmSpec &s = SPECS[i]; // this arm's limits
  uint16_t highest = 0;        // the highest RAISED_i of this arm
  for (uint8_t k = 0; k < N_SITES; k++)
  { // look at every site
    const uint16_t r = raised_pulse(i, k);
    if (r > highest)
      highest = r; // keep the largest
  }
  long t = (long)highest + (long)g_travel_us; // highest RAISED plus the extra
  if (t > (long)s.prox_zero)
    t = (long)s.prox_zero; // never above level
  return (uint16_t)t;      // as a pulse
}

// ---------------------------------------------------------------------------
// table_ok(): is this hand's taught table usable with the current P and S?
// Refuses (and says why, naming the site) when
//   - a site was never taught (AIM or CONTACT is 0);
//   - RAISED_i = CONTACT_i + S would be above level (the proximal ZERO);
//   - TOUCH_i  = CONTACT_i - P would be below the proximal floor;
//   - AIM_i is outside the distal limits.
// Example: CONTACT 1750, START 100 -> RAISED 1850 > 1830: refused, because the
// tap would start lower than asked (clamped at level) and the stroke would no
// longer be S + P long.
// ---------------------------------------------------------------------------
bool table_ok(size_t i)
{
  const ArmSpec &s = SPECS[i]; // this arm's limits and table
  for (uint8_t k = 0; k < N_SITES; k++)
  { // check every site
    const long aim = (long)s.sites->aim[k];         // taught distal pulse
    const long contact = (long)s.sites->contact[k]; // taught proximal pulse
    const char *why = nullptr;                      // the reason to refuse, if any
    if (aim == 0 || contact == 0)
      why = "not taught (0 in calibration.h)";
    else if (contact + (long)g_start_us > (long)s.prox_zero)
      why = "CONTACT + START is above level";
    else if (contact - (long)g_press_us < (long)s.prox_min)
      why = "CONTACT - PRESS is below the proximal floor";
    else if (aim < (long)s.dist_min || aim > (long)s.dist_zero)
      why = "AIM is outside the distal limits";
    if (why != nullptr)
    { // something wrong with this site
      Serial.print("ERR ");
      Serial.print(s.hand);
      Serial.print(" hand (");
      Serial.print(s.sites->who);
      Serial.print(") site ");
      Serial.print(k + 1); // 1-based, as you count them
      Serial.print(": ");
      Serial.println(why);
      return false;
    }
  }
  return true; // every site usable
}

// ---------------------------------------------------------------------------
// begin_select(): start stage A for the tap the arm is on. Part 1 is for the
// proximal to RISE to the travel height; the distal is not touched until the
// proximal is up there (update_arm moves on to parts 2 and 3 by itself). With
// TRAVEL 0 the travel height equals RAISED, so parts 1 and 3 do nothing.
// ---------------------------------------------------------------------------
void begin_select(size_t i)
{
  ArmRun &r = g_run[i];            // this arm's state
  r.prox_target = travel_pulse(i); // proximal to the travel height first
  r.state = State::Rising;         // stage A, part 1
}

// ---------------------------------------------------------------------------
// update_arm(): advance one arm's state machine. Called on EVERY pass of loop().
// Strokes are computed from the real elapsed time on each pass, so they follow
// the clock exactly; slewing is advanced only when slew_dt_s is non-zero.
//
// The moment a stroke must begin is t_go = planned contact - stroke duration.
// Worked example: contact planned at 3 500 000 us, stroke 50 000 us
//   -> t_go = 3 450 000 us; the tip should reach TOUCH at 3 500 000 us.
// (The real tip lags the command by the servo's own delay; Step 4 calibrates
// that. Here the planned contact is simply the end of the stroke.)
// ---------------------------------------------------------------------------
void update_arm(size_t i, uint32_t now, float slew_dt_s)
{
  const ArmSpec &s = SPECS[i]; // this arm's table
  ArmRun &r = g_run[i];        // and its state
  switch (r.state)
  {                // what stage are we in?
  case State::Off: // no pulses: nothing to do
    return;

  case State::Idle: // slew both joints toward their targets
    write_prox(i, slew_toward(r.prox_pos, (float)r.prox_target, PROX_RATE, slew_dt_s));
    write_dist(i, slew_toward(r.dist_pos, (float)r.dist_target, DIST_RATE, slew_dt_s));
    return;

  case State::Rising: // stage A, part 1: up to the travel height
    write_prox(i, slew_toward(r.prox_pos, (float)r.prox_target, PROX_RATE, slew_dt_s));
    if (r.prox_pos == (float)r.prox_target)
    {                                          // the proximal is up at the travel height:
      r.dist_target = s.sites->aim[r.plan_site[r.k]]; // NOW the distal may aim at this tap's site
      r.state = State::Selecting;              // part 2
    }
    return;

  case State::Selecting:                                                                // stage A, part 2: aim, with the arm clear of the finger
    write_prox(i, slew_toward(r.prox_pos, (float)r.prox_target, PROX_RATE, slew_dt_s)); // hold the travel height
    write_dist(i, slew_toward(r.dist_pos, (float)r.dist_target, DIST_RATE, slew_dt_s));
    if (r.dist_pos == (float)r.dist_target)
    {                                                    // the distal is over the site:
      r.prox_target = raised_pulse(i, r.plan_site[r.k]); // bring the proximal down to this site's RAISED
      r.state = State::Lowering;                         // part 3
    }
    return;

  case State::Lowering: // stage A, part 3: down to RAISED
    write_prox(i, slew_toward(r.prox_pos, (float)r.prox_target, PROX_RATE, slew_dt_s));
    if (r.prox_pos == (float)r.prox_target)
    {                         // at RAISED, aimed:
      r.state = State::Armed; // ready and waiting for the stroke time
    }
    return;

  case State::Armed:
  {                                                             // waiting for t_go
    const uint32_t t_go = r.plan_contact_us[r.k] - g_stroke_us; // when the stroke must start (unsigned wrap is fine)
    if (!clk::is_due(t_go, now))
      return;                                         // not yet: keep waiting (rollover-safe test)
    r.stroke_from = raised_pulse(i, r.plan_site[r.k]); // the stroke starts at this site's RAISED (CONTACT + S)
    r.stroke_to = touch_pulse(i, r.plan_site[r.k]);    // and ends at its TOUCH (CONTACT - P)
    r.t_stage_us = now;                               // the stroke begins now
    r.state = State::Descending;                      // stage B
    Serial.print("TAP k=");
    Serial.print(r.k); // one line per tap, like PROJECT.md 4.6
    Serial.print(" arm=");
    Serial.print(s.name);
    Serial.print("(");
    Serial.print(s.hand);
    Serial.print(")");
    Serial.print(" site=");
    Serial.print(r.plan_site[r.k] + 1); // 1-based, as you count them
    Serial.print(" t_planned_us=");
    Serial.print((uint32_t)(r.plan_contact_us[r.k] - g_trial_start_us)); // planned contact, from run start
    Serial.print(" t_cmd_us=");
    Serial.print((uint32_t)(now - g_trial_start_us)); // when the stroke was commanded, from run start
    Serial.print(" late_us=");
    Serial.println((int32_t)(now - t_go)); // how far past t_go we were: the scheduling lateness
    return;
  }

  case State::Descending:
  {                                                        // stage B: the downward stroke
    const uint32_t elapsed = now - r.t_stage_us;           // time since the stroke began (a duration)
    const float tau = (float)elapsed / (float)g_stroke_us; // 0 at the start, 1 at the end
    write_prox(i, (float)r.stroke_from + ((float)r.stroke_to - (float)r.stroke_from) * min_jerk(tau));
    if (elapsed >= g_stroke_us)
    {                           // stroke complete?
      r.state = State::Holding; // hold the tip down
      r.t_stage_us = now;       // the hold begins now
    }
    return;
  }

  case State::Holding: // tip down: wait out the hold time
    if (clk::is_due(r.t_stage_us + g_hold_us, now))
    {                              // hold finished? (rollover-safe test)
      r.stroke_from = r.stroke_to;                     // lift from where we are...
      r.stroke_to = raised_pulse(i, r.plan_site[r.k]); // ...back up to this site's RAISED
      r.t_stage_us = now;          // the lift begins now
      r.state = State::Lifting;    // stage C
    }
    return;

  case State::Lifting:
  {                                                    // stage C: the upward stroke
    const uint32_t elapsed = now - r.t_stage_us;       // time since the lift began (a duration)
    const float tau = (float)elapsed / (float)LIFT_US; // 0 at the start, 1 at the end
    write_prox(i, (float)r.stroke_from + ((float)r.stroke_to - (float)r.stroke_from) * min_jerk(tau));
    if (elapsed >= LIFT_US)
    {        // lift complete?
      r.k++; // on to the next tap
      if (r.k < r.n)
        begin_select(i); // more taps: stage A again, now for the next site
      else
        r.state = State::Idle; // none left: rest at the last site's RAISED
    }
    return;
  }
  }
}

// ---------------------------------------------------------------------------
// make_sequence(): fill seq[0..n-1] with random site numbers (0-based) such
// that (a) no site follows itself (U10 in PROJECT.md), and (b) when n is at
// least the number of sites, every site appears at least once (PROJECT.md 1.4).
//
// No immediate repeat, worked example (N_SITES = 5): the previous site was 2.
//   random(4) gives 0, 1, 2 or 3. 0 and 1 are used as they are; 2 and 3 are
//   bumped to 3 and 4. So the next site is 0, 1, 3 or 4, each with probability
//   1/4, and never 2.
// Rule (b) is met by trying again, up to 200 times, until all sites appear.
// ---------------------------------------------------------------------------
void make_sequence(uint8_t *seq, uint8_t n)
{
  for (int attempt = 0; attempt < 200; attempt++)
  {                    // a bounded number of tries
    unsigned used = 0; // bit mask of the sites used so far
    for (uint8_t k = 0; k < n; k++)
    {               // every tap in turn
      uint8_t site; // the site chosen for this tap
      if (k == 0)
      {
        site = (uint8_t)random((long)N_SITES); // first tap: any site, 0..4
      }
      else
      {
        site = (uint8_t)random((long)N_SITES - 1); // 0..3: one fewer choice than sites
        if (site >= seq[k - 1])
          site++; // step over the previous site
      }
      seq[k] = site;      // record it
      used |= 1u << site; // set that site's bit in the mask
    }
    if (n < N_SITES || used == ((1u << N_SITES) - 1u))
      return; // every site used (when possible): done
  }
}

// ---------------------------------------------------------------------------
// start_run(): schedule one run on BOTH arms and start stage A for tap 0.
//
// SIGN CONVENTION: t_rubber = t_real + delta. The real arm is the reference:
// its contacts are at base + k * PERIOD. The rubber arm's contacts are the
// same plus delta. Positive delta -> the rubber hand is touched LATER, i.e.
// the REAL hand is touched FIRST.
//
// The shared base time is far enough ahead (LEAD + |delta|) that even the
// earlier of the two arms still has the lead time for stage A.
// Worked example, delta = -300 ms (rubber first), now = 0, base = 1.5 + 0.3 s:
//   real   contacts at 1.8 s, 3.8 s, 5.8 s ...
//   rubber contacts at 1.5 s, 3.5 s, 5.5 s ...  (300 ms EARLIER than the real one)
// Returns false (and says why) if it cannot start.
// ---------------------------------------------------------------------------
bool start_run(const uint8_t *sites, uint8_t n, int32_t delta_ms, bool hidden)
{
  if (g_run_active)
  { // one run at a time
    Serial.println("ERR a run is going; type STOP first");
    return false;
  }
  for (size_t i = 0; i < N_ARMS; i++)
  { // both arms must be running and at rest
    if (g_run[i].state != State::Idle)
    {
      Serial.println("ERR send HOME first: both arms must be running and idle");
      return false;
    }
    if (!table_ok(i))
    { // both hands must be taught, with P and S inside the limits
      Serial.println("    teach it with arm_jog (docs/site-calibration.md), or change PRESS / START");
      return false;
    }
  }
  const int32_t delta_us = delta_ms * 1000;                                // ms -> us
  const uint32_t abs_us = (uint32_t)(delta_us < 0 ? -delta_us : delta_us); // |delta|
  const uint32_t now = clk::now_us();                                      // the clock reading everything hangs from
  const uint32_t base = now + LEAD_US + abs_us;                            // time of the first REAL-hand contact
  g_trial_start_us = now;                                                  // printed times are relative to this
  g_run_delta_ms = delta_ms;                                               // remembered for the DONE line
  g_run_hidden = hidden;                                                   // and whether to keep it secret until then
  for (size_t i = 0; i < N_ARMS; i++)
  { // schedule each arm
    ArmRun &r = g_run[i];
    const int32_t shift_us = (i == RUBBER) ? delta_us : 0; // t_rubber = t_real + delta; the real arm gets no shift
    for (uint8_t k = 0; k < n; k++)
    {                                                                             // every tap
      r.plan_site[k] = sites[k];                                                  // both arms use the SAME site
      r.plan_contact_us[k] = base + (uint32_t)k * PERIOD_US + (uint32_t)shift_us; // planned contact; unsigned wrap handles a negative shift
    }
    r.n = n;         // how many taps
    r.k = 0;         // start with the first
    begin_select(i); // stage A for tap 0
  }
  g_run_active = true; // the run is live
  g_run_id++;          // a new run number
  return true;
}

// ---------------------------------------------------------------------------
// abort_all(): stop whatever is happening and lift both arms to ZERO (level).
// The distal stops where it is. Safe to call at any moment, even mid-stroke: the
// proximal then slews up from wherever it is, at PROX_RATE.
// ---------------------------------------------------------------------------
void abort_all()
{
  bool any = false; // did any arm have pulses?
  for (size_t i = 0; i < N_ARMS; i++)
  {
    ArmRun &r = g_run[i];
    if (r.state == State::Off)
      continue; // no pulses: nothing to lift
    any = true;
    r.prox_target = SPECS[i].prox_zero;            // head for level
    r.dist_target = (uint16_t)(r.dist_pos + 0.5f); // distal: stay where it is
    r.state = State::Idle;                         // no stage running any more
  }
  g_run_active = false; // the run is over
  if (any)
    Serial.println("STOP: lifting both arms to ZERO");
  else
    Serial.println("STOP: nothing to stop, the arms are off");
}

// ---------------------------------------------------------------------------
// do_home(): HOME. An arm that is Off gets its START PULSES at once (this is
// the same as typing the four start pulses in arm_jog: the arm is assumed to
// be sitting there already). An arm that is already running slews back to its
// start pose.
// ---------------------------------------------------------------------------
void do_home()
{
  if (g_run_active)
  { // not in the middle of a run
    Serial.println("ERR a run is going; type STOP first");
    return;
  }
  bool first = false; // did any arm just start?
  for (size_t i = 0; i < N_ARMS; i++)
  {
    const ArmSpec &s = SPECS[i];
    ArmRun &r = g_run[i];
    r.prox_target = s.prox_zero; // both joints head for ZERO
    r.dist_target = s.dist_zero;
    if (r.state == State::Off)
    {                  // first time: start the pulses at once
      r.prox_sent = 0; // force the first write to reach the hardware
      r.dist_sent = 0;
      write_prox(i, (float)s.prox_zero); // proximal at level
      write_dist(i, (float)s.dist_zero); // distal at its start pulse
      first = true;
    }
    r.state = State::Idle; // running, at rest
  }
  if (first)
    Serial.println("HOME: start pulses are now going out. Switch the SERVO POWER ON.");
  else
    Serial.println("HOME: returning both arms to their start poses");
}

// ---------------------------------------------------------------------------
// check_run_done(): called from loop(). When a run is active and both arms have
// come to rest, print DONE (and the delta, if it was being kept secret).
// ---------------------------------------------------------------------------
void check_run_done()
{
  if (!g_run_active)
    return; // nothing to finish
  for (size_t i = 0; i < N_ARMS; i++)
  {
    if (g_run[i].state != State::Idle)
      return; // an arm is still busy
  }
  g_run_active = false; // the run is over
  Serial.print("DONE id=");
  Serial.print(g_run_id);
  Serial.print(" asyn_ms=");
  Serial.println(g_run_delta_ms); // always shown now, even if it was hidden
}

// ---------------------------------------------------------------------------
// parse_int(): read a whole number from text, strictly ("15x" is refused).
// ---------------------------------------------------------------------------
bool parse_int(const char *text, long &out)
{
  char *end = nullptr;                       // strtol fills this in
  const long value = strtol(text, &end, 10); // base 10; understands + and -
  if (end == text || *end != '\0')
    return false; // no digits, or junk after them
  out = value;
  return true;
}

// ---------------------------------------------------------------------------
// split(): cut a line into words IN PLACE, without copying and without heap.
//   "RAISED top 1670" -> tok[0]="RAISED" tok[1]="top" tok[2]="1670", returns 3
// ---------------------------------------------------------------------------
size_t split(char *line, char *tok[], size_t max_tok)
{
  size_t n = 0;   // words found so far
  char *p = line; // read position in the line
  while (*p != '\0' && n < max_tok)
  { // until the end or the word limit
    while (*p == ' ')
    {
      *p = '\0';
      p++;
    } // turn spaces into terminators, skip them
    if (*p == '\0')
      break;    // only spaces were left: done
    tok[n] = p; // a word starts here
    n++;        // count it
    while (*p != '\0' && *p != ' ')
      p++; // walk to the end of the word
  }
  return n;
}

// ---------------------------------------------------------------------------
// print_list(): print the five site pulses as "a,b,c,d,e" (used by SHOW).
// ---------------------------------------------------------------------------
void print_list(const uint16_t *v)
{
  for (size_t k = 0; k < N_SITES; k++)
  {
    if (k > 0)
      Serial.print(','); // comma between values
    Serial.print(v[k]);
  }
}

// ---------------------------------------------------------------------------
// print_computed(): print one computed pulse per site, "a,b,c,d,e", using a
// function such as raised_pulse or touch_pulse (used by SHOW).
// Example, CONTACT 1620,1610,1600,1590,1580 and START 100, with raised_pulse:
//   1720,1710,1700,1690,1680
// ---------------------------------------------------------------------------
void print_computed(size_t i, uint16_t (*pulse_of)(size_t, uint8_t))
{
  for (uint8_t k = 0; k < N_SITES; k++)
  {
    if (k > 0)
      Serial.print(',');           // comma between values
    Serial.print(pulse_of(i, k));  // this site's computed pulse
  }
}

// ---------------------------------------------------------------------------
// show(): print every arm and every setting.
// ---------------------------------------------------------------------------
void show()
{
  for (size_t i = 0; i < N_ARMS; i++)
  {
    const ArmSpec &s = SPECS[i];
    const ArmRun &r = g_run[i];
    Serial.print(s.name);
    Serial.print(" (");
    Serial.print(s.hand);
    Serial.print(" hand)");
    Serial.print("  state=");
    Serial.print(STATE_NAMES[(uint8_t)r.state]);
    if (r.state != State::Off)
    { // pulses mean nothing while off
      Serial.print("  prox=");
      Serial.print(r.prox_sent);
      Serial.print("  dist=");
      Serial.print(r.dist_sent);
    }
    Serial.print("  travel=");
    Serial.println(travel_pulse(i)); // the height really used between touches
    Serial.print("    taught: ");
    Serial.print(s.sites->who);      // whose table it is
    Serial.print(", ");
    Serial.println(s.sites->date);   // and when it was taught
    Serial.print("    aim=");
    print_list(s.sites->aim);        // taught distal pulses, sites 1-5
    Serial.print("  contact=");
    print_list(s.sites->contact);    // taught proximal pulses, sites 1-5
    Serial.println();
    Serial.print("    raised=");
    print_computed(i, raised_pulse); // CONTACT + S, sites 1-5
    Serial.print("  touch=");
    print_computed(i, touch_pulse);  // CONTACT - P, sites 1-5
    Serial.println();
  }
  Serial.print("asyn=");
  if (g_asyn_random)
    Serial.print("RANDOM (hidden until DONE)");
  else
  {
    Serial.print(g_asyn_ms);
    Serial.print(" ms (positive = real hand first)");
  }
  Serial.print("  hold=");
  Serial.print(g_hold_us / 1000);
  Serial.print(" ms");
  Serial.print("  press(P)=");
  Serial.print(g_press_us);
  Serial.print(" us");
  Serial.print("  start(S)=");
  Serial.print(g_start_us);
  Serial.print(" us");
  Serial.print("  travel+=");
  Serial.print(g_travel_us);
  Serial.print(" us");
  Serial.print("  stroke=");
  Serial.print(g_stroke_us / 1000);
  Serial.println(" ms");
}

// ---------------------------------------------------------------------------
// pick_delta(): the asynchrony for the next run. With ASYN RANDOM it picks one
// of the levels at random and asks for it to be hidden until DONE.
// ---------------------------------------------------------------------------
void pick_delta(int32_t &delta_ms, bool &hidden)
{
  if (g_asyn_random)
  {                                               // random mode
    randomSeed(micros());                         // seed from the clock: the time of your keypress is unpredictable
    delta_ms = LEVELS_MS[random((long)N_LEVELS)]; // one of the seven levels
    hidden = true;                                // keep it secret until DONE
  }
  else
  {                       // fixed mode
    delta_ms = g_asyn_ms; // whatever ASYN set
    hidden = false;
  }
}

// ---------------------------------------------------------------------------
// print_delta(): the "asyn_ms=" part of a header, or "hidden".
// ---------------------------------------------------------------------------
void print_delta(int32_t delta_ms, bool hidden)
{
  Serial.print(" asyn_ms=");
  if (hidden)
    Serial.println("hidden"); // revealed in the DONE line
  else
    Serial.println(delta_ms);
}

// ---------------------------------------------------------------------------
// cmd_one(): ONE <site> and RAND: a single tap on one site, both arms.
// ---------------------------------------------------------------------------
void cmd_one(uint8_t site)
{
  int32_t delta_ms = 0; // the delta for this run
  bool hidden = false;  // whether to hide it
  pick_delta(delta_ms, hidden);
  if (!start_run(&site, 1, delta_ms, hidden))
    return; // a run of ONE tap
  Serial.print("ONE id=");
  Serial.print(g_run_id);
  Serial.print(" site=");
  Serial.print(site + 1); // 1-based
  print_delta(delta_ms, hidden);
}

// ---------------------------------------------------------------------------
// cmd_trial(): TRIAL: 6 taps per arm, one shared random site sequence.
// ---------------------------------------------------------------------------
void cmd_trial()
{
  randomSeed(micros());                  // seed from the clock (see pick_delta)
  uint8_t sites[N_TAPS];                 // the site of each tap
  make_sequence(sites, (uint8_t)N_TAPS); // random, no immediate repeat, all sites used
  int32_t delta_ms = 0;
  bool hidden = false;
  pick_delta(delta_ms, hidden);
  if (!start_run(sites, (uint8_t)N_TAPS, delta_ms, hidden))
    return;
  Serial.print("TRIAL id=");
  Serial.print(g_run_id);
  Serial.print(" sites=");
  for (size_t k = 0; k < N_TAPS; k++)
  { // the sequence, 1-based, comma separated
    if (k > 0)
      Serial.print(',');
    Serial.print(sites[k] + 1);
  }
  print_delta(delta_ms, hidden);
}

// ---------------------------------------------------------------------------
// handle_line(): act on one complete command. The first word decides which.
// ---------------------------------------------------------------------------
void handle_line(char *line)
{
  char *tok[4];                         // pointers to the words
  const size_t n = split(line, tok, 4); // cut the line into words
  if (n == 0)
    return;   // nothing but spaces
  long v = 0; // a parsed number, when a command has one

  if (strcasecmp(tok[0], "HOME") == 0 && n == 1)
  {
    do_home();
    return;
  }
  if (strcasecmp(tok[0], "STOP") == 0 && n == 1)
  {
    abort_all();
    return;
  }
  if (strcasecmp(tok[0], "SHOW") == 0 && n == 1)
  {
    show();
    return;
  }
  if (strcasecmp(tok[0], "TRIAL") == 0 && n == 1)
  {
    cmd_trial();
    return;
  }

  if (strcasecmp(tok[0], "RAND") == 0 && n == 1)
  {                                          // RAND: one tap, random site
    randomSeed(micros());                    // seed from the clock
    cmd_one((uint8_t)random((long)N_SITES)); // any of the five sites
    return;
  }

  if (strcasecmp(tok[0], "ONE") == 0 && n == 2)
  { // ONE <1-5>
    if (!parse_int(tok[1], v) || v < 1 || v > (long)N_SITES)
    {
      Serial.println("ERR ONE needs a site from 1 to 5, e.g. ONE 3");
      return;
    }
    cmd_one((uint8_t)(v - 1)); // typed 1-5 -> internal 0-4
    return;
  }

  if (strcasecmp(tok[0], "ASYN") == 0 && n == 2)
  { // ASYN <ms> | ASYN RANDOM
    if (strcasecmp(tok[1], "RANDOM") == 0)
    {
      g_asyn_random = true; // pick a level per run, hidden
    }
    else if (parse_int(tok[1], v) && v >= -1000 && v <= 1000)
    {
      g_asyn_random = false; // a fixed delta
      g_asyn_ms = (int32_t)v;
    }
    else
    {
      Serial.println("ERR ASYN needs -1000 to 1000 (ms) or RANDOM, e.g. ASYN 150");
      return;
    }
    show(); // confirm
    return;
  }

  if (strcasecmp(tok[0], "HOLD") == 0 && n == 2)
  { // HOLD <ms>
    if (!parse_int(tok[1], v) || v < 20 || v > 1000)
    {
      Serial.println("ERR HOLD needs 20 to 1000 (ms), e.g. HOLD 120");
      return;
    }
    g_hold_us = (uint32_t)v * 1000u; // ms -> us
    show();
    return;
  }

  if (strcasecmp(tok[0], "PRESS") == 0 && n == 2)
  { // PRESS <us>
    if (!parse_int(tok[1], v) || v < 0 || v > 60)
    {
      Serial.println("ERR PRESS needs 0 to 60 (us), e.g. PRESS 10");
      return;
    }
    if (g_run_active)
    { // P is part of every planned stroke: not mid-run
      Serial.println("ERR a run is going; type STOP first");
      return;
    }
    g_press_us = (uint16_t)v; // P, for both hands; takes effect at the next run
    show();
    return;
  }

  if (strcasecmp(tok[0], "START") == 0 && n == 2)
  { // START <us>
    if (!parse_int(tok[1], v) || v < 30 || v > 200)
    {
      Serial.println("ERR START needs 30 to 200 (us above CONTACT), e.g. START 100");
      return;
    }
    if (g_run_active)
    { // S is part of every planned stroke: not mid-run
      Serial.println("ERR a run is going; type STOP first");
      return;
    }
    g_start_us = (uint16_t)v; // S, for both hands; takes effect at the next run
    show();
    return;
  }

  if (strcasecmp(tok[0], "TRAVEL") == 0 && n == 2)
  { // TRAVEL <us>
    if (!parse_int(tok[1], v) || v < 0 || v > 200)
    {
      Serial.println("ERR TRAVEL needs 0 to 200 (us above RAISED), e.g. TRAVEL 100");
      return;
    }
    if (g_run_active)
    {
      Serial.println("ERR a run is going; type STOP first");
      return;
    }
    g_travel_us = (uint16_t)v; // takes effect at the next run
    show();
    return;
  }

  if (strcasecmp(tok[0], "STROKE") == 0 && n == 2)
  { // STROKE <ms>
    if (!parse_int(tok[1], v) || v < 20 || v > 500)
    {
      Serial.println("ERR STROKE needs 20 to 500 (ms), e.g. STROKE 50");
      return;
    }
    g_stroke_us = (uint32_t)v * 1000u; // ms -> us
    show();
    return;
  }

  Serial.println("ERR could not understand that. Commands:"); // anything else
  Serial.println("    HOME, SHOW, STOP (or '!'), ONE <1-5>, RAND, TRIAL");
  Serial.println("    ASYN <ms>|RANDOM, HOLD <ms>, PRESS <us>, START <us>, STROKE <ms>, TRAVEL <us>");
}

// ---------------------------------------------------------------------------
// read_serial(): collect characters into lines without ever waiting, like
// HostLink::poll(). Two characters act IMMEDIATELY, with no Enter, because
// stopping must never wait: '!' anywhere, and a space typed at the start of an
// empty line.
// ---------------------------------------------------------------------------
void read_serial()
{
  while (Serial.available() > 0)
  {                                     // only what has already arrived
    const char c = (char)Serial.read(); // take one character
    if (c == '!' || (c == ' ' && g_len == 0))
    { // the emergency stop
      abort_all();
      continue; // next character
    }
    if (c == '\n')
    { // end of a command
      if (g_overflow)
      {
        Serial.println("ERR line too long");
      }
      else if (g_len > 0)
      {                       // ignore empty lines
        g_line[g_len] = '\0'; // terminate the C string
        handle_line(g_line);
      }
      g_len = 0; // start the next line fresh
      g_overflow = false;
    }
    else if (c == '\r')
    {
      // Windows sends \r\n; throw the \r away.
    }
    else if (g_len < LINE_SIZE - 1)
    { // room left (one slot kept for '\0')
      g_line[g_len] = c;
      g_len = g_len + 1;
    }
    else
    {
      g_overflow = true; // full: drop it, report at '\n'
    }
  }
}

// ---------------------------------------------------------------------------
// setup(): runs once at power-up. Sets each pin's frequency and leaves every
// joint with NO pulses, so nothing moves until you type HOME. Type SHOW to see
// the taught tables; a run is refused while either still has a 0.
// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);            // USB serial; the number is nominal
  analogWriteResolution(PWM_BITS); // 14 bits for every PWM pin
  for (size_t i = 0; i < N_ARMS; i++)
  {
    const ArmSpec &s = SPECS[i];
    pinMode(s.pin_prox, OUTPUT); // the pins drive the signal wires
    pinMode(s.pin_dist, OUTPUT);
    analogWriteFrequency(s.pin_prox, (float)PROX_HZ); // pins 2 and 3 share a timer: 333 Hz
    analogWriteFrequency(s.pin_dist, (float)DIST_HZ); // pins 6 and 9 share another: 100 Hz
    analogWrite(s.pin_prox, 0);                       // 0 steps HIGH: no pulses yet
    analogWrite(s.pin_dist, 0);
  }
  g_last_slew_us = clk::now_us(); // start the slew clock now
}

// ---------------------------------------------------------------------------
// loop(): runs forever. Read commands, work out whether a slew tick is due, and
// advance both arms. Nothing here ever waits.
// ---------------------------------------------------------------------------
void loop()
{
  read_serial();                      // commands, without waiting
  const uint32_t now = clk::now_us(); // one clock reading for this whole pass
  float slew_dt_s = 0.0f;             // zero unless a slew tick is due
  if (clk::is_due(g_last_slew_us + SLEW_TICK_US, now))
  {                                                                // has a tick period passed? (rollover-safe)
    slew_dt_s = (float)(uint32_t)(now - g_last_slew_us) * 1.0e-6f; // elapsed time in seconds
    g_last_slew_us = now;                                          // the next tick is counted from here
  }
  for (size_t i = 0; i < N_ARMS; i++)
  {
    update_arm(i, now, slew_dt_s); // advance each arm's state machine
  }
  check_run_done(); // announce the end of a run
}
