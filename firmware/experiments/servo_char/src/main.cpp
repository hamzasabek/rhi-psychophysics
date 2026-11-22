// ===========================================================================
//  servo_char - test program for roadmap Step 3.1 (characterise the servos)
//
//  WHAT IT DOES
//  ------------
//  Drives ONE servo on SERVO_PIN and lets you control it by typing commands
//  in the serial monitor. Unlike the Arduino Servo library, which is fixed at
//  50 pulses per second, it lets you choose the pulse rate - which is exactly
//  what Step 3.1 needs to test.
//
//  COMMANDS (type one, press Enter)
//  --------------------------------
//    HZ <n>     pulses per second, 20 to 400.      e.g.  HZ 333
//    US <n>     pulse length in microseconds,      e.g.  US 1520
//               500 to 2500. Starts the pulses.
//    OFF        stop the pulses (signal stays LOW).
//    SHOW       print the current settings.
//
//  SAFETY
//  ------
//  At power-up the program sends NO pulses, so the servo does not jump to
//  some position on its own. Nothing moves until you type a US command.
//  Test with only a horn (and a pointer) on the servo, never the arm.
//
//  HOW A PULSE IS MADE - worked example
//  ------------------------------------
//  The Teensy's PWM hardware repeats a frame over and over. Each frame starts
//  HIGH and goes LOW after a chosen fraction of the frame. We tell it that
//  fraction as a whole number of "steps": with 14-bit resolution a frame is
//  split into 2^14 = 16384 steps.
//
//      frame length   = 1 000 000 us / HZ
//      duty (steps)   = pulse_us * HZ * 16384 / 1 000 000   (rounded)
//
//  At HZ 333, US 1520:
//      frame          = 1 000 000 / 333            = 3003 us
//      duty           = 1520 * 333 * 16384 / 10^6  = 8292.9  ->  8293 steps
//      real pulse     = 8293 * 10^6 / (333 * 16384) = 1520.0 us
//  At HZ 50, US 1520:
//      frame          = 20 000 us
//      duty           = 1520 * 50 * 16384 / 10^6   = 1245.2  ->  1245 steps
//      real pulse     = 1245 * 10^6 / (50 * 16384)  = 1519.8 us
//  The difference between asked and real comes from rounding to whole steps.
//  SHOW prints both, so you always know what the servo is really receiving.
//
//  Project rules kept even in a throwaway: no delay(), no heap allocation.
// ===========================================================================

#include <Arduino.h>   // Serial, analogWrite, analogWriteFrequency, ...
#include <cstdlib>     // strtoul(): text -> number
#include <cstring>     // strcmp(), strncmp(): compare C strings

// ---------------------------------------------------------------------------
// Settings that never change while the program runs.
// ---------------------------------------------------------------------------
constexpr uint8_t  SERVO_PIN = 2;      // servo signal, through a 1 kohm resistor
constexpr uint8_t  PWM_BITS  = 14;     // resolution: 2^14 = 16384 steps per frame
constexpr uint32_t PWM_STEPS = 1u << PWM_BITS;   // 16384 (1 shifted left 14 times)

constexpr uint32_t HZ_MIN = 20;        // slower than any servo standard
constexpr uint32_t HZ_MAX = 400;       // a little above the PDI's rated 330 Hz
constexpr uint32_t US_MIN = 500;       // widest range any hobby servo uses
constexpr uint32_t US_MAX = 2500;

// ---------------------------------------------------------------------------
// The program's current state. One copy, set by commands.
// ---------------------------------------------------------------------------
uint32_t g_hz       = 50;      // pulses per second; 50 is the classic standard
uint32_t g_pulse_us = 1520;    // pulse length; 1520 is the PDI's centre
bool     g_on       = false;   // false = no pulses (safe start-up)

// Line buffer, the same idea as HostLink: collect characters until '\n'.
constexpr size_t LINE_SIZE = 64;   // 63 characters + the '\0' terminator
char   g_line[LINE_SIZE];          // the characters collected so far
size_t g_len      = 0;             // how many there are
bool   g_overflow = false;         // true if this line was too long

// ---------------------------------------------------------------------------
// duty_for(): turn a pulse length into a number of PWM steps.
//
// Uses 64-bit arithmetic because the middle of the sum is big:
//   2500 * 400 * 16384 = 16 384 000 000, which does not fit in 32 bits
//   (the 32-bit maximum is 4 294 967 295).
// Adding 500 000 before dividing by 1 000 000 rounds to the nearest step
// instead of always rounding down. Example: 8292.9 -> 8293, not 8292.
// ---------------------------------------------------------------------------
uint32_t duty_for(uint32_t pulse_us, uint32_t hz) {
  const uint64_t scaled = (uint64_t)pulse_us * hz * PWM_STEPS;   // us*Hz*steps
  return (uint32_t)((scaled + 500000u) / 1000000u);              // round, -> steps
}

// ---------------------------------------------------------------------------
// real_pulse_us(): the pulse length the servo really gets for a given duty.
// The reverse of duty_for(), without rounding. Example: 8293 steps at 333 Hz
// -> 8293 * 1 000 000 / (333 * 16384) = 1520.0 us.
// ---------------------------------------------------------------------------
float real_pulse_us(uint32_t duty, uint32_t hz) {
  return (float)duty * 1000000.0f / ((float)hz * (float)PWM_STEPS);
}

// ---------------------------------------------------------------------------
// apply(): send the current settings to the PWM hardware.
// Called after every command that changes something.
// ---------------------------------------------------------------------------
void apply() {
  analogWriteFrequency(SERVO_PIN, (float)g_hz);   // set the frame rate
  if (g_on) {
    analogWrite(SERVO_PIN, duty_for(g_pulse_us, g_hz));   // pulses on
  } else {
    analogWrite(SERVO_PIN, 0);    // 0 steps HIGH = the wire stays LOW: no pulses
  }
}

// ---------------------------------------------------------------------------
// show(): print every setting, including what the servo REALLY receives.
// Example output:
//   hz=333 frame_us=3003 pulse_us=1520 duty=8293/16384 real_us=1520.0 on=yes
// ---------------------------------------------------------------------------
void show() {
  const uint32_t duty = duty_for(g_pulse_us, g_hz);   // steps for this pulse
  Serial.print("hz=");        Serial.print(g_hz);
  Serial.print(" frame_us="); Serial.print(1000000u / g_hz);   // frame length
  Serial.print(" pulse_us="); Serial.print(g_pulse_us);         // what was asked
  Serial.print(" duty=");     Serial.print(duty);
  Serial.print("/");          Serial.print(PWM_STEPS);
  Serial.print(" real_us=");  Serial.print(real_pulse_us(duty, g_hz), 1);
  Serial.print(" on=");       Serial.println(g_on ? "yes" : "no");
}

// ---------------------------------------------------------------------------
// parse_number(): read a whole number from text, strictly.
//
// strtoul ("string to unsigned long") reads digits and sets `end` to the
// first character it did NOT use. We accept the number only if at least one
// digit was read (end moved) and nothing is left after it (*end == '\0').
//   "333"  -> ok, 333
//   "33x"  -> refused (the 'x' is left over)
//   ""     -> refused (no digits)
// Returns true and fills `out` on success.
// ---------------------------------------------------------------------------
bool parse_number(const char* text, uint32_t& out) {
  char* end = nullptr;                                  // strtoul fills this in
  const unsigned long value = strtoul(text, &end, 10);  // base 10
  if (end == text || *end != '\0') return false;        // no digits, or junk after
  out = (uint32_t)value;
  return true;
}

// ---------------------------------------------------------------------------
// handle_line(): act on one complete command.
// ---------------------------------------------------------------------------
void handle_line(const char* line) {
  uint32_t n = 0;   // the number after the command word, if any

  if (strncmp(line, "HZ ", 3) == 0) {                // "HZ " + number
    if (!parse_number(line + 3, n) || n < HZ_MIN || n > HZ_MAX) {
      Serial.println("ERR HZ needs a number from 20 to 400, e.g. HZ 333");
      return;
    }
    if (g_on && g_pulse_us >= 1000000u / n) {        // pulse longer than the frame?
      Serial.println("ERR the current pulse does not fit in that frame");
      return;
    }
    g_hz = n;
    apply();
    show();
    return;
  }

  if (strncmp(line, "US ", 3) == 0) {                // "US " + number
    if (!parse_number(line + 3, n) || n < US_MIN || n > US_MAX) {
      Serial.println("ERR US needs a number from 500 to 2500, e.g. US 1520");
      return;
    }
    if (n >= 1000000u / g_hz) {                      // the pulse must end before
      Serial.println("ERR pulse longer than the frame; lower HZ first");
      return;                                        // the next frame begins
    }
    g_pulse_us = n;
    g_on = true;                                     // a US command starts pulses
    apply();
    show();
    return;
  }

  if (strcmp(line, "OFF") == 0) {                    // exactly "OFF"
    g_on = false;
    apply();
    show();
    return;
  }

  if (strcmp(line, "SHOW") == 0) {                   // exactly "SHOW"
    show();
    return;
  }

  Serial.print("ERR unknown command: ");             // anything else
  Serial.println(line);
  Serial.println("    commands: HZ <n>, US <n>, OFF, SHOW");
}

// ---------------------------------------------------------------------------
// setup(): runs once at power-up.
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);                  // USB serial; the number is nominal
  analogWriteResolution(PWM_BITS);       // 14 bits for every PWM pin
  pinMode(SERVO_PIN, OUTPUT);
  apply();                               // g_on is false: start with NO pulses
}

// ---------------------------------------------------------------------------
// loop(): runs forever. Collect characters into lines without ever waiting,
// exactly like HostLink::poll().
// ---------------------------------------------------------------------------
void loop() {
  while (Serial.available() > 0) {       // only what has already arrived
    const char c = (char)Serial.read();

    if (c == '\n') {                     // end of a command
      if (g_overflow) {
        Serial.println("ERR line too long");
      } else if (g_len > 0) {            // ignore empty lines
        g_line[g_len] = '\0';            // terminate the C string
        handle_line(g_line);
      }
      g_len = 0;                         // start the next line fresh
      g_overflow = false;
    } else if (c == '\r') {
      // Windows sends \r\n; throw the \r away.
    } else if (g_len < LINE_SIZE - 1) {  // room left (one slot kept for '\0')
      g_line[g_len] = c;
      g_len = g_len + 1;
    } else {
      g_overflow = true;                 // full: drop it, report at '\n'
    }
  }
}
