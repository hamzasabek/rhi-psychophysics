# Arm geometry and site angles

Fill in during roadmap step 3.2. Photograph the arm at each site and save the
images in `docs/figures/`.

![Side view of the arm: the raised, aim and touch settings found for each of the five finger sites](figures/arm-site-angles.svg)

Three settings are found by hand for each arm: **RAISED** (proximal, one value
for all sites), **AIM** (distal, one per site) and **TOUCH** (proximal, one per
site). They are found with `firmware/experiments/arm_jog`; its header lists the
commands. Record the pulses in microseconds first: a pulse is exactly what the
servo receives, whereas an angle depends on where the horn happens to sit on
its splined shaft. Degrees are optional (see the note under the tables).

Date: 2026-11-24 (bench session started)
Link lengths: proximal 175 mm (19 mm wide); distal 112.1 mm in total, including
the 30 mm rubber tip tube (measured 2026-11-24). Before the tube was fitted the
distal link was 90 mm; PROJECT.md 1.2 and the figures still say 90 mm and are to
be updated.
Base position relative to hand:

## Wiring (decided 2026-11-23)

| Arm | Joint | Servo | Teensy pin | Pulse rate |
|---|---|---|---|---|
| top | proximal | PDI-4409MG | 3 | 333 Hz |
| top | distal | EMAX ES9257 | 6 | 100 Hz |
| down | proximal | PDI-4409MG | 2 | 333 Hz |
| down | distal | EMAX ES9257 | 9 | 100 Hz |

Each signal goes through a 1 kohm resistor until the 74AHCT125 arrives. Pins 2
and 3 are the A and B channels of one hardware PWM timer, and pins 6 and 9 are
the A and B channels of another (read from the pin table in the Teensy core).
Pins on one timer share a frequency, so the PDIs can run at 333 Hz and the
EMAXes at 100 Hz (the EMAX buzzes at 200 Hz and above, see
`servo-characterisation.md`). Not yet verified on the logic analyser: expect a
3.00 ms period on pins 2 and 3, and 10.0 ms on pins 6 and 9.

## Limits carried over from step 3.1

- EMAX operating range 900-1900 us. It shakes slightly at 1200 us, so avoid
  that pulse for a site.
- The five sites need about 53 degrees of distal sweep, which is about 550 us
  at the EMAX's 0.0956 degrees per us. Turn the horn on its spline so the five
  AIM pulses fall in the clean part, roughly 1250-1800 us.
- The jog program limits the PDI to 600-1830 us: the top is the ZERO below, and
  the floor is inside the 500-2500 us measured. Narrow the floor once the arm's
  real safe swing is known.

## ZERO poses (2026-11-24)

Each joint has a ZERO pulse: the pose every trial starts from, and a ceiling the
joint is never commanded above. Fewer microseconds move the joint DOWN.

| Joint | ZERO | Notes |
|---|---|---|
| top proximal (PDI, pin 3) | 1830 us | link level, checked with a level |
| down proximal (PDI, pin 2) | 1830 us | link level, checked with a level |
| top distal (EMAX, pin 6) | 1900 us (decided, horn not yet fitted) | a few degrees below horizontal |
| down distal (EMAX, pin 9) | 1860 us (decided, horn not yet fitted) | a few degrees below horizontal, about 4 degrees further than the top arm |

- The proximal horns were fitted with the pulse held at 1800 us, then trimmed up
  by 30 us (about 2.8 degrees at 0.093 degrees per us) until a level read 0.
- Direction, checked on the bench: 1780 us drops the proximal arm and 1830 us
  raises it, so fewer microseconds is down.
- Ceiling: the jog program refuses any proximal pulse above 1830 us. It is a
  software limit only; there is no physical stop, to keep the EMAX gears away
  from hard stops.
- Smallest visible step on the proximal arm, judged by eye: no motion at 1810 and
  1820 us (10 and 20 us from ZERO), motion at 1800 us (30 us, about 2.8
  degrees). This is the effective resolution under load, not a measured dead
  band. Approach every TOUCH pulse from above (descending), the direction the
  real tap travels, in case there is backlash.
- Distal plan: ZERO is 1900 us for the top arm and 1860 us for the down arm, with
  the link a few degrees below horizontal so that the farthest site (site 1) can
  be reached. The down arm starts 40 us (about 3.8 degrees) lower, so its link
  must sit about 4 degrees further below horizontal than the top arm's for site 1
  to stay above the 900 us floor. Each horn is fitted at its ZERO pulse; then
  check that site 1 is reachable at 900 us or more, aiming for about 950 us (the
  EMAX is unreliable at the ends of its range). If it is not, refit the horn one
  tooth over.
- The jog program caps the EMAXes at their ZERO: 1900 us (top) and 1860 us (down).

## Site mapping (2026-11-24): an alternative to Chancel et al.

Chancel et al. use five sites on the index finger (PROJECT.md 1.4). This rig
uses FOUR, about 13.3 mm apart, the same four on both arms. It is recorded as
deviation D6 in PROJECT.md 1.13.

| Site | Location, as given on 2026-11-24 |
|---|---|
| 1 | the MCP joint: the articulation between the metacarpal and the proximal phalanx |
| 2 | between site 1 and site 3 |
| 3 | between site 2 and site 4 |
| 4 | the last site; given as "the middle proximal phalanx joint", to be confirmed |

On the top arm the distal AIM pulse falls as the site number rises (1260 us at
site 1, 1200 us at site 2, 1130 us at site 3, 1080 us at site 4), so fewer
microseconds moves the tip from site 1 toward site 4.

## Per-site settings

Four sites, numbered from the MCP joint (see "Site mapping" above). The
Location column and the fifth row of each table below come from the original
five-site layout and are not used; fill in four rows per arm. The figure
`figures/arm-site-angles.svg` also shows that original layout (five sites, 2 cm
apart) and is out of date; redo it once both arms are measured.

### Top arm (PDI on pin 3, EMAX on pin 6)

RAISED, proximal pulse (same for all sites): ___ us (___ deg, optional)

| Site | Location | AIM: distal pulse (us) | AIM angle (deg, optional) | TOUCH: proximal pulse (us) | TOUCH angle (deg, optional) |
|---|---|---|---|---|---|
| 1 | proximal phalanx | | | | |
| 2 | PIP joint | | | | |
| 3 | middle phalanx | | | | |
| 4 | DIP joint | | | | |
| 5 | distal phalanx | | | | |

### Down arm (PDI on pin 2, EMAX on pin 9)

RAISED, proximal pulse (same for all sites): ___ us (___ deg, optional)

| Site | Location | AIM: distal pulse (us) | AIM angle (deg, optional) | TOUCH: proximal pulse (us) | TOUCH angle (deg, optional) |
|---|---|---|---|---|---|
| 1 | proximal phalanx | | | | |
| 2 | PIP joint | | | | |
| 3 | middle phalanx | | | | |
| 4 | DIP joint | | | | |
| 5 | distal phalanx | | | | |

**About the degree columns.** For the EMAX, a provisional conversion is
`angle = 102.8 - 0.0943 x (pulse - 1450)` (protractor convention, from
`servo-characterisation.md`; the angle falls as the pulse grows). For the PDI,
the angle at a given pulse depends on where the horn was fitted, so measure it
from the horizontal with a level instead of reusing the step 3.1 protractor
numbers. The sign convention for the real arm is set in the servo driver
(step 3.3).

**Raw log.** Paste the `MARK` lines from the jog program here, one per
settled pose, so every number above can be traced:

```
MARK zero_top_prox top_prox=1830 top_dist=1800 down_prox=1830 down_dist=off
MARK zero_down_prox top_prox=1830 top_dist=1800 down_prox=1830 down_dist=off
(paste further MARK lines here)
```

Photographs: save in `docs/figures/` and list them here: ___

## Arc compensation check

Predicted height variation across sites (sagitta, see PROJECT.md 2.2): ~9 mm
(90 mm distal link sweeping an ~80 mm finger chord: 80² / (8 × 90) = 8.9 mm).
With the 112.1 mm distal link now fitted this becomes 80² / (8 × 112.1) = 7.1 mm;
the table below still uses the 90 mm figures and is to be redone.

![Tip arc against a flat finger, with the estimated extra proximal rotation per site](figures/arm-tip-arc.svg)

Predicted extra proximal rotation to touch, compared with site 3. Assumes a
flat finger, the arm near horizontal and site 1 nearest the base:

| Site | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Arc gap above the finger (mm) | 9.4 | 2.2 | 0 | 2.2 | 9.4 |
| Extra rotation (deg) | +4.0 | +0.8 | 0 | +0.7 | +2.5 |
| Same in PDI pulse (us, at 0.093 deg per us) | about 43 | about 9 | 0 | about 7 | about 27 |

How the estimate is made: lowering the tip by the gap g needs a proximal
rotation of g divided by the horizontal distance from the proximal shaft to the
tip. That distance is about 13.5 cm at site 1 and 21.5 cm at site 5 (17.5 cm
link plus or minus the tip's offset along the finger). Site 1: 9.4 mm / 135 mm
= 0.070 rad = 4.0 deg. Site 5: 9.4 mm / 215 mm = 0.044 rad = 2.5 deg. A real
finger is not flat, so expect the measured numbers to differ; that is the
reason to measure them.

Observed spread in proximal TOUCH pulses, largest minus smallest: ___ us
(___ deg)
