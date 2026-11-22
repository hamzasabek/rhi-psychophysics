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
| Max refresh rate (Hz) | 330 | at least 333 | 2026-11-22. Tested at 333 Hz only, pulse 1520 us: no buzz, no heat after 2 min, reads 84-85 deg with no real drift (1 deg is within reading error), smooth moves between 1000 and 2000 us. Signal checked on the logic analyser: period 3.00 ms, pulse 1.52 ms. Lower rates were not tested because 333 Hz passed. Not resolved: 2000 us read about 40 deg at 50 Hz and about 45 deg at 333 Hz, with reading precision of 1-2 deg on a hand protractor; whether the rate moves the end position was not tested. |
| Dead band (us) | 2 | not measured | 1 us is about 0.09 deg, finer than the hand protractor can show. Skipped on 2026-11-22; contact-time repeatability on the arm is the test that matters. |
| Time for 30 deg step (ms) | ~55 @ 6V | not measured | Skipped on 2026-11-22. A bare horn does not carry the arm's inertia, so the figure would not describe the real stroke. The command-to-contact delay is measured on the arm with the logic analyser and contact sensor (Step 3.5 onwards). |
| Overshoot | - | not measured | Same reason as the step response. |
| Idle current (mA) | - | not measured | Not needed for the design: the supply is sized for the peak current of four servos (PROJECT.md section 2.6), not for idle. |

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
| Usable pulse range (us) | not published | about 800-2100; **use 900-1900** | 2026-11-22, 50 Hz, horn only. The horn moves between about 800 and 2100 us. Near both ends the servo is unreliable: small commands sometimes produce no movement, and after being sent to an end (700 us, 2200 us) it did not respond when commanded 50-100 us back. Operating range is limited to 900-1900 us, 100 us inside each end, so the plastic gears are never driven into the stop. The ends were found by when the horn stopped moving, not read on the protractor, so each is uncertain by about 50 us. At 50 Hz, untouched, the servo shakes slightly at 1200 us (about 125 deg) and at no other pulse tested; the position there still repeats (125 deg on both visits). Treat 1200 us as a pulse to avoid for a site, and check any site near it on the arm in Step 3.2. |
| Angular range (deg) | not published | about 106 between 900 and 2000 us (156 to 50 deg); about 123 over 800-2100 us (extrapolated) | 2026-11-22, 50 Hz, horn only. **Provisional.** Readings and fit are in "EMAX angle measurement" below. Angle falls as the pulse grows. Straight-line fit: 0.0943 deg per us (10.6 us per deg), every reading within 2.5 deg of it (rms 1.4 deg). The first session gave 0.104 deg per us and read 5-7 deg differently above 1300 us, so the slope is quoted as 0.09-0.10. The 123 deg figure extends the fit to 800 and 2100 us and was not measured. Revisits to the same pulse agree to 0-5 deg (mean 2.4 deg), about the size of the 5 deg reading step, so the servo itself may repeat better than this; a laser pointer or long pointer repeat would show it. |
| Centre pulse (us) | not published | about 1450 | The midpoint of the measured ends (800 and 2100 us), so uncertain by up to about 50 us. Not confirmed independently. |
| Max refresh rate (Hz) | **not published** | between 100 and 200 | 2026-11-22, horn only, same routine as the PDI. Rates tried in order: 100, 200, 333 Hz (all angle readings above were taken at 50 Hz). 100 Hz (frame 10 ms): moves smoothly. 200 Hz (frame 5.1 ms as read): buzzes when moving. 333 Hz: also buzzes. 150 Hz was not tried, so the limit is only known to lie between 100 and 200 Hz. Heat and drift after a 2 minute hold at 100 Hz were not written down. |
| Dead band (us) | not published | not measured | 1 us is about 0.1 deg, finer than the hand protractor can show. Skipped on 2026-11-22; contact-time repeatability on the arm is the test that matters. |
| Time for 30 deg step (ms) | ~40 @ 4.8V | not measured | Skipped on 2026-11-22. The EMAX only repositions during the 2 s gap between taps, so there is a very large margin, and a bare horn does not carry the distal link. Ringing after a move is checked on the arm (Step 3.5). |
| Overshoot | - | not measured | Same reason as the step response. |
| Idle current (mA) | - | about 10 (5-15) | 2026-11-22, 100 Hz, holding 1400 us, 5 V, horn only. Read on the meter's 10 A range, which steps in 0.01 A, so 0.01 A means 5-15 mA. The meter's mA jack read 0, probably a blown fuse; check it before using that range again. |

### EMAX angle measurement (2026-11-22)

**Method.** As for the PDI above: servo clamped, shaft horizontal, horn only (no arm), protractor upright in the swing plane, pointer taped to the horn, 50 Hz, pulses started before the servo power was switched on, each angle read by eye after the `US` command. This is the second session, with the protractor set up again after the first one was badly positioned. Most readings are multiples of 5 deg, so the scale was probably read to the nearest 5 deg tick (precision about +/-2.5 deg).

**Readings, in the order they were taken.**

| Pulse (us) | Angle read (deg) |
|---|---|
| 1500 | 100 |
| 1600 | 90 |
| 1700 | 80 |
| 1800 | 70 |
| 1900 | 60 |
| 2000 | 50 |
| 1400 | 110 |
| 1300 | 115 |
| 1200 | 125 |
| 1100 | 135 |
| 1000 | 145 |
| 900 | 156 |
| 1500 (revisit) | 96 |
| 1500 (revisit) | 95 |
| 1600 (revisit) | 88 |
| 1700 (revisit) | 78 |
| 1400 (revisit) | 106 |
| 1300 (revisit) | 116 |
| 1200 (revisit) | 125 |
| 900 (revisit) | 159 |

**Straight-line fit** through all 13 readings (least squares):

```
angle (deg) = 102.8 - 0.0943 x (pulse_us - 1450)
pulse (us)  = 1450 + (102.8 - angle_deg) / 0.0943
```

Worked example: pulse 1700 us -> 102.8 - 0.0943 x 250 = 79.2 deg. Pulse needed for 90 deg -> 1450 + (102.8 - 90) / 0.0943 = 1586 us.

| Pulse (us) | Read (deg) | Fitted (deg) | Read minus fitted |
|---|---|---|---|
| 900 | 156 | 154.7 | +1.3 |
| 1000 | 145 | 145.3 | -0.3 |
| 1100 | 135 | 135.8 | -0.8 |
| 1200 | 125 | 126.4 | -1.4 |
| 1300 | 115 | 117.0 | -2.0 |
| 1400 | 110 | 107.5 | +2.5 |
| 1500 | 100 and 96 | 98.1 | +1.9 and -2.1 |
| 1600 | 90 | 88.7 | +1.3 |
| 1700 | 80 | 79.2 | +0.8 |
| 1800 | 70 | 69.8 | +0.2 |
| 1900 | 60 | 60.4 | -0.4 |
| 2000 | 50 | 50.9 | -0.9 |

Pulse for a chosen angle, from the fit (outside 900-1900 us is outside the operating range, see above):

| Angle (deg) | 150 | 140 | 130 | 120 | 110 | 100 | 90 | 80 | 70 | 60 |
|---|---|---|---|---|---|---|---|---|---|---|
| Pulse (us) | 950 | 1056 | 1162 | 1268 | 1374 | 1480 | 1586 | 1692 | 1798 | 1904 |

**Revisits.** The last seven rows of the readings table were taken later, at pulses already read. Revisits to the same pulse agree to 0-5 deg (spread by pulse: 900 -> 3, 1200 -> 0, 1300 -> 1, 1400 -> 4, 1500 -> 5, 1600 -> 2, 1700 -> 2), mean 2.4 deg. That is about the size of the 5 deg reading step, so these spreads are an upper bound on how much the servo itself wanders. The fit above uses only the first 13 readings. Pooling all 20 readings gives slope 0.0956 deg per us (10.5 us per deg) and 102.6 deg at 1450 us (rms 1.7 deg, largest miss 3.8 deg, at 900 us), which is within about 1 deg of the fit above anywhere in the range, so the fit is not changed. Readings at 900 us (156, 159) sit about 2 deg above the line; within the reading error, noted only.

**First session, for comparison** (pulse us -> deg): 900 -> 160, 1000 -> 147 (142 on a revisit), 1100 -> 131 (132 on a revisit), 1200 -> 125, 1300 -> 115, 1400 -> 104, 1500 -> 94 (94 on a revisit), 1600 -> 85, 1700 -> 74, 1800 -> 63, 1900 -> 53. Slope 0.104 deg per us. The two sessions agree within 4 deg from 900 to 1300 us and differ by 5-7 deg from 1400 to 1900 us. The same pulse also read 4-5 deg differently on revisits (1000 us: 147 and 142; 1500 us: 100 and 96). Most likely cause: the protractor centre is not on the shaft axis, which gives an error that changes with angle, plus reading to the nearest 5 deg. That the servo itself is at fault is not shown. This is why the angular range is provisional.

**Next.** Repeat with a laser pointer taped to the horn and a wall or paper about 2 m away (spot moves about 3.5 cm per degree), or a long pointer over graph paper.

## Decisions

- Proximal (PDI) refresh rate: 333 Hz, because it passed every check (no buzz, no heat, no drift, smooth moves, clean signal on the logic analyser) and it is the servo's rated maximum, so it gives the shortest wait for a new command. Angles for the real arm are calibrated at this rate in Step 3.2, so the unresolved 5 deg difference seen against 50 Hz does not affect the decision.
- Distal (ES9257) refresh rate: 100 Hz, because it moved smoothly there while 200 Hz and 333 Hz buzzed when moving, and the distal joint only selects the site during the 2 s gap between taps, so the 10 ms worst-case wait for a new command costs nothing. The EMAX pins must therefore sit on a different FlexPWM submodule from the PDI pins (333 Hz), see PROJECT.md section 2.8, and the two frequencies are to be verified on the logic analyser. Operating pulse range: 900-1900 us (see the EMAX table above).
- Step response, overshoot and dead band were not measured on the bare servos (2026-11-22), nor was the PDI idle current, nor a 2 minute hold at 100 Hz for the EMAX. Reasons: a bare horn does not carry the arm's inertia, so a bare stroke time would not describe the real tap; 1 us is finer than the hand protractor reads; and the timing figures that matter (command-to-contact delay and its spread) are measured directly on the arm. Revisit if tap repeatability or distal ringing turns out to be a problem.
