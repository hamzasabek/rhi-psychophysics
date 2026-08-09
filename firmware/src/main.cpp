// ===========================================================================
//  main.cpp - Step 1.2: PlatformIO project that blinks
//
//  Purpose of this file: prove the toolchain works end to end (write code,
//  compile it, upload it, see it run) and establish the ONE rule that holds
//  for the entire rest of this firmware:
//
//      NEVER call delay(). Ever. For any reason.
//
//  delay() freezes the whole processor for its duration - it cannot read
//  the serial port, service an interrupt, or update a servo while "asleep"
//  inside a delay(). Our real firmware will need to do all three of those
//  things while also blinking, tapping, and listening, all "at once" - so
//  we practise the non-blocking pattern here, on the simplest possible task,
//  before we need it for anything that actually matters.
// ===========================================================================

#include <Arduino.h>
// ^ Pulls in the Arduino framework: pinMode(), digitalWrite(), the
//   elapsedMillis type, and the (hidden) main() that calls setup() once
//   and then loop() forever. We very rarely need to #include this
//   explicitly - PlatformIO usually does it for us - but writing it out
//   makes clear where these names come from.

// ---- Configuration ---------------------------------------------------------

// The Teensy 4.1 has a built-in LED wired to a specific pin. Arduino/Teensy
// headers define this constant for us, so we don't have to hunt through a
// datasheet to find which physical pin number it is.
constexpr uint8_t LED_PIN = LED_BUILTIN;

// How long the LED stays in each state (on, then off), in milliseconds.
// constexpr means "the compiler computes and fixes this value at compile
// time" - it costs nothing at runtime, and the compiler will catch it if
// we accidentally try to change it later.
constexpr unsigned long BLINK_INTERVAL_MS = 3000;

// ---- State ------------------------------------------------------------------

// elapsedMillis is a Teensyduino type that behaves like an integer, but
// counts UPWARD BY ITSELF over real time, driven by a hardware timer -
// we never write code to increment it. Assigning it a value (like 0)
// doesn't "stop the clock"; it just resets what the count is measured
// FROM. So at any moment, since_toggle tells us "how many milliseconds
// have passed since I last reset this variable to 0".
//
// This is declared here, outside setup() and loop(), so its value
// PERSISTS between calls to loop() - loop() runs thousands of times a
// second, and we need to remember how much time has passed across all
// of those calls, not just within one of them.
elapsedMillis since_toggle;

// Tracks whether the LED is currently on or off, so toggle_led() knows
// which way to flip it. Also persists between loop() calls for the same
// reason as since_toggle above.
bool led_is_on = false;

// ---- Functions ----------------------------------------------------------

// Flips the LED to the opposite of its current state.
//
// Kept as its own small function rather than inlined into loop() because
// naming the action ("toggle_led") makes loop() read like a description
// of the behaviour, rather than a pile of low-level pin-twiddling. This
// habit - give the small operation a name, keep loop() readable - is one
// we'll lean on heavily once loop() has several different jobs to juggle.
void toggle_led()
{
    led_is_on = !led_is_on;           // flip our record of the state
    digitalWrite(LED_PIN, led_is_on); // and make the physical pin match it
}

// setup() runs exactly ONCE, right after power-on or a reset - before
// loop() ever runs. This is where we configure things that only need
// configuring a single time.
void setup()
{
    // Tell the microcontroller this pin will be used to SEND a signal out
    // (as opposed to INPUT, which would be for reading a signal in, e.g.
    // from a button or sensor). Every pin must be configured with pinMode()
    // before you use it - skipping this is a very common first bug.
    pinMode(LED_PIN, OUTPUT);
}

// loop() runs over and over, forever, as fast as the processor can manage -
// typically many thousands of times per second for a function this small.
// Nothing pauses it between iterations UNLESS we write code that blocks,
// which is precisely the thing we are practising not doing.
void loop()
{
    // "Has at least BLINK_INTERVAL_MS passed since we last toggled?"
    // Because since_toggle counts up on its own, this comparison is enough -
    // we don't need to remember a separate "last toggle time" and subtract.
    if (since_toggle >= BLINK_INTERVAL_MS)
    {
        since_toggle = 0; // reset the count - starts measuring again from now
        toggle_led();     // do the actual work for this tick
    }

    // Execution reaches here on EVERY pass through loop(), whether or not
    // the interval had elapsed. That is the entire point: loop() is never
    // stuck waiting. In later steps, this is where servo updates, serial
    // parsing, and event-queue checks will all live side by side, each
    // guarded by its own "has enough time passed?" check, exactly like the
    // one above.
}