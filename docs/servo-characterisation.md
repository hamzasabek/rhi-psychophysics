# Servo characterisation

Measured, not copied from a datasheet. Fill in during roadmap step 3.1.

Date:
Supply voltage:
Ambient temperature:
Instrument:

## Safety check: voltage on the signal wire (2026-10-20)

**Why.** Teensy 4.1 pins are not 5 V tolerant. Some servos pull their signal
wire up towards their own 5 V supply; connected directly, that voltage would
flow back into the Teensy pin and slowly damage it. The 74AHCT125 buffer
(PROJECT.md section 2.7) prevents this, but it has not arrived yet, so we
measured whether a direct connection is safe in the meantime.

**Method.** Servo powered from the 5 V supply (red to +5 V, brown/black to
GND), signal wire left unconnected. Multimeter on DC volts, black probe on
GND, red probe on the signal wire.

| Servo | Signal wire voltage | Verdict |
|---|---|---|
| JX PDI-4409MG | 50 mV | safe (far below the 0.5 V limit we set) |
| EMAX ES9257 | 0 V | safe |

**Decision.** Neither servo pushes voltage back. Until the 74AHCT125
arrives, the Teensy pin drives the servo signal directly through a 1 kohm
series resistor, which limits the current if anything goes wrong. Earlier
informal use had already shown the servos respond to 3.3 V pulses; watch for
random twitching during the tests below, which would mean they do not.

## Observation: noise at power-up (2026-10-20, PDI-4409MG)

Test program: `firmware/experiments/servo_char`, 50 Hz, horn only, no load.

| Situation | What happened |
|---|---|
| Servo power on, **no pulses yet** | buzzes for about 500 ms. Sound only: the horn does not move. |
| Pulses already running (`US 1520`), **then** servo power on | quiet at power-on. Sometimes one short noise a few seconds later, then silent. No movement. |
| Sending pulses (`US ...`) | moves smoothly to each position |

**Why.** A digital servo has its own small controller. At power-up it starts
before it knows what position is wanted, and the motor receives a few
uncontrolled pushes while it settles. If pulses are already present, it
knows the target from its first moment.

**Power-up rule for the rig.** Start the pulses first, then switch on the
servo power. This matters once the arm is attached and a finger is
underneath it.

## JX PDI-4409MG

| Measurement | Published | Measured | Notes |
|---|---|---|---|
| Usable pulse range (us) | 500-2500 | at least 500-2500 | 2026-10-20, 50 Hz, horn only. Still moving between 2400 and 2500 and between 600 and 500, quiet at both ends. The test program stops at 500/2500, so the servo may go slightly further; we do not need it to. |
| Angular range (deg) | 120-180 | | |
| Centre pulse (us) | 1520 | | |
| Max refresh rate (Hz) | 330 | | |
| Dead band (us) | 2 | | |
| Time for 30 deg step (ms) | ~55 @ 6V | | |
| Overshoot | - | | |
| Idle current (mA) | - | | |

## EMAX ES9257

| Measurement | Published | Measured | Notes |
|---|---|---|---|
| Usable pulse range (us) | not published | | |
| Angular range (deg) | not published | | |
| Centre pulse (us) | not published | | |
| Max refresh rate (Hz) | **not published** | | **the key unknown** |
| Dead band (us) | not published | | |
| Time for 30 deg step (ms) | ~40 @ 4.8V | | |
| Overshoot | - | | |
| Idle current (mA) | - | | |

## Decisions

- Proximal (PDI) refresh rate:  Hz, because
- Distal (ES9257) refresh rate:  Hz, because
