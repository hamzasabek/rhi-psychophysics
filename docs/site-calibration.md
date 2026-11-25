# Site calibration: teaching the five sites by contact

This is the procedure that tells each arm where the five stimulation sites
are. It is done **once for the rubber hand** and **once per participant for the
real hand**, at the start of the session, before any trial.

Decided 2026-11-25. It replaces the fixed four-site mapping of deviation D6
(withdrawn, see PROJECT.md 1.13).

## 1. The idea in one paragraph

The five sites are anatomical, as in Chancel et al. (PROJECT.md 1.4), so on
every finger they sit in a different place and at a different height: the
knuckle (MCP joint) of a relaxed palm-down hand is well above the table and the
finger slopes down to the fingertip. Instead of modelling that, we **mark the
sites on the finger and teach the arm to touch each mark**. The arm then
"learns" the finger's length, slope and thickness directly. For each site the
experimenter writes down only two numbers; everything else is computed with two
constants that are the same for every participant.

## 2. The two numbers measured per site

Each joint has one job, so each site needs one number per joint:

```
            proximal joint (PDI)  - chooses HOW LOW (it makes the tap)
  base ●══════════════════════●  distal joint (EMAX) - chooses WHERE along the finger
                               ╲
                                ╲
                                 ▼ tube
        ─────────────────────────────────── finger
          S1     S2     S3     S4     S5
```

| Name | Joint | Meaning |
|---|---|---|
| **AIM_i** | distal (EMAX) | the distal pulse that puts the tube directly above the pen dot of site *i* |
| **CONTACT_i** | proximal (PDI) | with the distal held at AIM_i, the proximal pulse at which the tube **first touches the skin** |

So a hand is described by 2 numbers x 5 sites = 10 numbers.

## 3. The two constants, the same for everyone

On the proximal joint, more microseconds = higher, fewer = lower.

| Name | Meaning | Starting value |
|---|---|---|
| **S** (start height) | how far ABOVE the skin the tube waits just before a tap | 100 us |
| **P** (press) | how far PAST the skin the arm is commanded, so the tap presses | 10 us |

From them the program computes, for every site:

```
RAISED_i = CONTACT_i + S      the tap starts here
TOUCH_i  = CONTACT_i - P      the tap ends here
```

Worked example, site 3, CONTACT_3 = 1610 us, S = 100 us, P = 10 us:

```
 pulse (us)
   1710   ●  RAISED_3 = 1610 + 100    waits here, about 3 cm above the skin
          │      ↓  the tap: one fast stroke
   1610   ●  CONTACT_3                the tube first touches the skin (measured)
   1600   ●  TOUCH_3  = 1610 - 10     where the arm is told to go
```

At the reach of the arm (about 200 mm from the proximal shaft) 10 us on the
proximal joint is about 0.93 degrees, which moves the tube about 3 mm. So
S = 100 us is about 3 cm of start height, and P = 10 us is about 3 mm of
commanded press. The tube cannot really go 3 mm into the finger: the skin
squashes a little and the servo keeps pushing. A larger P is a firmer press.

**Why it is done this way.** The tap is always RAISED_i to TOUCH_i, which is
S + P = 110 us at every site and for every participant. The same stroke means
the same movement and therefore the same delay from command to contact, which
is the property design D4 is built on (PROJECT.md 2.2). And the same P gives
every participant the same press.

P and S are open parameters (PROJECT.md 1.12, U1 and U4) and will be fixed
once, for all participants. One thing to check when fixing P: the same P gives
roughly the same servo torque at every site, but the force at the tube is the
torque divided by the reach, so the sites farther from the shaft are pressed a
little more lightly (about +-12 % between 175 mm and 225 mm of reach). A
kitchen scale under a dummy finger at S1 and S5 shows whether that matters.

Between taps the arm rises higher still, to the TRAVEL height (the highest
RAISED_i of that arm plus TRAVEL), so the distal joint can swing from one site
to the next without brushing the finger.

## 4. Placing the five pen dots

The tube is 7 mm wide, so its radius is r = 3.5 mm.

1. Hand palm down on the palm rest, relaxed, index finger straight in the
   groove, fingertip lightly against the stop. Do not press against the stop:
   the finger pulp squashes by 1 to 2 mm and every site moves with it.
2. Find the three joints on the BACK of the finger (the tube taps the back):
   ask for a slight bend, feel the joint line, mark it with a thin line of
   washable skin pen, then straighten the finger back into the groove.
   Mark the nail fold (the skin edge at the base of the nail) the same way.
3. Put a dot on the midline of the finger for each site:

| Site | Chancel et al. | Where the dot goes |
|---|---|---|
| S1 | proximal phalanx | halfway between the MCP and PIP lines (measure with a ruler) |
| S2 | PIP joint | on the PIP line |
| S3 | middle phalanx | halfway between the PIP and DIP lines |
| S4 | DIP joint | on the DIP line |
| S5 | distal phalanx just proximal to the nail | 3.5 mm behind the nail fold, so the tube's front edge just reaches it |

Worked example (the numbers are illustrative): measured from the fingertip,
nail fold 13 mm, DIP 22 mm, PIP 46 mm, MCP 86 mm. Then S5 = 13 + 3.5 =
16.5 mm, S4 = 22 mm, S3 = (22 + 46) / 2 = 34 mm, S2 = 46 mm and S1 =
(46 + 86) / 2 = 66 mm. The spacings are 5.5, 12, 12 and 20 mm.

On a short distal phalanx S4 and S5 can be closer than the 7 mm tube is wide
(5.5 mm above). That is a consequence of the anatomy; write it down in the
session notes when it happens.

The rubber hand is marked the same way, once, with a permanent fine marker.

## 5. Teaching one hand with arm_jog

The program is `firmware/experiments/arm_jog` (its header lists every command).
Upload it with the servo power OFF, open `pio device monitor`, and send the
start pulses (the ZERO poses, `docs/geometry.md`), one line each:

```
top prox 1830
top dist 1900
down prox 1830
down dist 1860
```

Hold the arms near level, then switch the servo power on. Type `KEYS` for
single-key mode. Key mode never starts a joint: a key on a joint that has no
pulses yet answers `ERR that joint is off`.

Select the arm: `1` = top arm (rubber hand), `2` = down arm (real hand).

For each site, 1 to 5:

1. **Aim.** With the proximal well up (`u` lifts it to level), move the
   distal with `a` / `d` until the tube is directly above the dot. Look from
   the side AND from above.
2. **Approach.** Lower the proximal with `s`, at the 30 us step while the tube
   is far away, then at 10 us and finally 5 us (`z` / `x` change the step).
3. **Contact criterion** (the same for every experimenter): lay a thin strip of
   paper on the dot. CONTACT is the first pulse at which the strip can no
   longer slide freely under the tube. Do not ask the participant "do you feel
   it": touch thresholds differ between people and would enter the calibration.
4. **Check from above.** Press `u` (lift) and then `b` (come back down to the
   same pulse, from above, the direction a tap travels). The strip must be
   caught again. If not, go one step lower and repeat.
5. **Record.** Press `c` and then the site number (`1` to `5`). The program
   stores the distal pulse as AIM and the proximal pulse as CONTACT and prints
   them.
6. Lift (`u`) before going to the next site.

When all five sites are recorded, press `p`: the program prints a block of
code for the selected arm, ready to paste (section 6). Copy it from the
monitor, and keep the full monitor log with the session notes.

## 6. Where the numbers go

`firmware/experiments/common/calibration.h` holds:

- `RUBBER`: the rubber hand's table, taught once, changed only if the rubber
  hand or its mounting is moved;
- `PARTICIPANT`: the current participant's table, replaced at the start of
  each session;
- `PRESS_US` (P) and `START_US` (S), the same for everyone.

Paste the block printed by `p` over the matching table, fill in the participant
code and the date, rebuild and upload `arm_taps`. `arm_taps` refuses to tap a
hand whose table still contains a zero (a site not taught), or whose numbers
fall outside the joint limits.

This header is the bench version of calibration storage. In the real firmware
the host sends the same numbers with `CAL SET` (PROJECT.md Step 4.1), and they
are logged with the session data.

## 7. Check: the ink test

With `arm_taps` uploaded and HOME done:

1. Put a little washable ink on the end of the tube.
2. `ONE 1` to `ONE 5`: one tap on each site, both arms.
3. Every ink mark must land on its dot, within **+-2 mm** (about half the tube
   width). Note the error per site.

A site that misses is taught again (section 5) before the session goes on.
At the end of the session, tap one site again: if it has moved, the posture
drifted during the session; note it.

## 8. Per-session record

| Field | Value |
|---|---|
| Participant code | |
| Date | |
| Hand | real / rubber |
| Landmarks from fingertip (mm): nail fold, DIP, PIP, MCP | |
| AIM (us), sites 1-5 | |
| CONTACT (us), sites 1-5 | |
| P, S (us) | |
| Ink test error per site (mm) | |
| End-of-session check | |
| Notes (for example S4 and S5 overlapping) | |

## 9. Taught tables

RAISED and TOUCH are computed by `arm_taps` with S = 100 us and P = 10 us
(RAISED = CONTACT + 100, TOUCH = CONTACT - 10); they are listed here so the
numbers can be checked against `SHOW`.

### Rubber hand (top arm, PDI on pin 3, EMAX on pin 6)

Taught 2026-11-25. Travel height with TRAVEL 100: 1700 + 100 = 1800 us.

| Site | AIM (us) | CONTACT (us) | RAISED (us) | TOUCH (us) |
|---|---|---|---|---|
| 1 | 1265 | 1600 | 1700 | 1590 |
| 2 | 1185 | 1590 | 1690 | 1580 |
| 3 | 1140 | 1550 | 1650 | 1540 |
| 4 | 1130 | 1530 | 1630 | 1520 |
| 5 | 1110 | 1505 | 1605 | 1495 |

To watch in the ink test:
- Sites 3 and 4 are only 10 us apart on the distal (about 1 degree); check
  that their marks land on separate dots.
- Site 2 (1185 us) is 15 us from 1200 us, where the EMAX shakes slightly.

### Test participant (the author, real hand, down arm, PDI on pin 2, EMAX on pin 9)

Taught 2026-11-26. Travel height with TRAVEL 100: 1720 + 100 = 1820 us.

| Site | AIM (us) | CONTACT (us) | RAISED (us) | TOUCH (us) |
|---|---|---|---|---|
| 1 | 1170 | 1620 | 1720 | 1610 |
| 2 | 1050 | 1610 | 1710 | 1600 |
| 3 | 990 | 1575 | 1675 | 1565 |
| 4 | 980 | 1540 | 1640 | 1530 |
| 5 | 1000 | 1515 | 1615 | 1505 |

To watch in the ink test:
- Sites 3 and 4 are only 10 us apart on the distal, as on the rubber hand.
- Site 5's AIM (1000 us) is above site 4's (980 us), the reverse of every
  other step toward the tip; check that the site 5 mark lands beyond site 4's.
- Sites 3 to 5 sit at 980-1000 us, near the EMAX's 900 us floor, where it is
  less reliable; if those marks scatter, refit the down distal horn one tooth
  over.

Ink test results: not yet done.
