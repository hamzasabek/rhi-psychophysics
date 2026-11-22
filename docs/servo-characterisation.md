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
| Angular range (deg) | 120-180 | about 186 | 2026-11-22, 50 Hz, horn only. 180 deg at 500 us, about -6 deg at 2500 us (past the 0 deg edge of the protractor, so that end is estimated). More than published. Angle falls as the pulse grows. See "PDI angle measurement" below. |
| Centre pulse (us) | 1520 | about 1500 | The midpoint of the measured range (87 deg) falls at about 1500 us. 1520 us read 85 deg. Not yet confirmed independently, because the 2500 us end is estimated. |
| Max refresh rate (Hz) | 330 | | |
| Dead band (us) | 2 | | |
| Time for 30 deg step (ms) | ~55 @ 6V | | |
| Overshoot | - | | |
| Idle current (mA) | - | | |

### PDI angle measurement (2026-11-22)

**Method.** Servo clamped on its post with the shaft horizontal, horn only (no
arm). Protractor fixed upright in the swing plane, centre mark on the shaft
axis, 0 deg at the level edge toward the hand. Pointer taped to the horn.
Horn fitted with the servo holding 1520 us. Pulse rate 50 Hz. Pulses started
before the servo power was switched on. Each angle read by eye looking straight
along the shaft, one second after the `US` command. Reading precision about
1 deg.

| Pulse (us) | Angle read (deg) |
|---|---|
| 500 | 180 |
| 1000 | 133 |
| 1250 | 110 (check point, predicted from the slope before reading) |
| 1520 | 85 |
| 2000 | 40 |
| 2500 | about -6 (past the scale, estimated) |

**Result.** The response is a straight line: the slope is 0.092 to 0.094 deg
per us in every interval, about 10.8 us per degree. Over 500 to 2500 us that is
about 186 deg. The angle on the protractor decreases as the pulse grows, which
is only a matter of how the protractor is read; the sign convention for the
real arm is set in the servo driver (Step 3.3).

**Limits of this measurement.** The 2500 us end is estimated, because the pointer
passed the 0 deg edge of the protractor. Repeat with the horn turned one tooth
if a measured value is needed. The 1520 us reading of 85 deg depends on where
the horn happened to sit on the shaft teeth, so it is not a servo property.

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
