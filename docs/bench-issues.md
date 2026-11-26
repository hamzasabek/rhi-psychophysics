# Bench issues

Problems seen on the rig, what probably causes them, and what to try. Each
issue stays here until it is closed, with the date and what fixed it.

Status words: **Open** (not yet worked on), **Testing** (a fix is being
tried), **Closed** (fixed, with the evidence).

## 0. First suspect for issues 2, 3 and 4: power and signal

Issues 2, 3 and 4 all look like a servo that does not follow its command
cleanly. Two parts of the electrical build in PROJECT.md 2.6 and 2.7 are still
unticked in `hardware/BOM.md`, and either one alone can cause all three:

| Part | Planned (PROJECT.md) | Now | What goes wrong without it |
|---|---|---|---|
| Servo supply | separate 5 V, at least 6 A | not recorded (`servo-characterisation.md` leaves "Supply voltage" blank) | four servos starting together draw current spikes; the voltage dips and a servo stalls, resets or jitters |
| Bulk capacitor | 1000 uF low-ESR at the servo connectors, plus 100 nF ceramics | not fitted | the same dips, with nothing to smooth them |
| Signal buffer | 74AHCT125: 3.3 V in, clean 5 V out | each signal goes straight from the Teensy through 1 kohm | a 3.3 V pulse is close to the servo's HIGH threshold; with the resistor and long wires the servo can misread the pulse width, so it shakes or ignores small changes |
| Ground | one star point shared by the Teensy and the servo supply | to check | without a shared ground the servo reads the pulse against the wrong 0 V |

**Do first, before any other fix below:**
1. Write down the servo supply (make, voltage, current rating) in
   `servo-characterisation.md`. Measure the 5 V rail at the servo connectors
   with a multimeter while both arms move: it should stay above 4.8 V.
2. Fit the 1000 uF capacitor (watch its polarity) and the 100 nF ceramics
   across 5 V and GND at the servo connectors.
3. Fit the 74AHCT125 (arrival test in `hardware/BOM.md`) and keep the signal
   wires short, each twisted with a ground wire.
4. Check that the Teensy GND and the servo supply GND meet at one point.

Then repeat issues 2, 3 and 4. Whatever is still there is mechanical or
software, and the fixes below apply.

**A cheap measuring tool for issues 2, 3 and 4: a laser pointer.** Tape a
laser pointer along the link and let it shine on a wall about 2 m away. At
2 m, 1 degree moves the spot about 35 mm, so a 5 us step on the PDI (about
0.47 degrees) moves it about 16 mm. Mark the spot on paper taped to the wall.
It shows whether a joint really moved, by how much, whether it comes back to
the same place, and how big a shake is.

---

## Issue 1: sites 4 and 5 are hard to reach; the tip slides toward the fingertip

**Seen (2026-11-26, while teaching).** For S4 and S5 the distal is folded a
long way relative to the proximal, and when the arm comes down the tip drifts
toward the tip of the index finger.

**Status.** Open.

**Probable cause: the tap does not come down onto the skin, it comes in at a
slant.** The proximal joint turns the whole arm about the proximal shaft, so
the tip always moves at right angles to the line from that shaft to the tip.
The tap is straight down only when the tip is level with the shaft. The
further the tip sits below the shaft, the more the tap slides along the
finger instead of pressing into it.

Estimate from the taught pulses (PDI 0.093 degrees per us down from level at
1830 us; EMAX 0.0943 degrees per us from its ZERO; distal ZERO taken as in
line with the proximal link; proximal link 175 mm, distal 112.1 mm):

| | Proximal below level | Distal folded from ZERO | Tube direction | Tip below the shaft | Tap direction above the skin |
|---|---|---|---|---|---|
| Top arm, S1 (prox 1600, dist 1265) | 21 deg | 60 deg | 81 deg below horizontal | about 175 mm | about 46 deg |
| Down arm, S5 (prox 1515, dist 1000) | 29 deg | 81 deg | about 110 deg: past vertical, leaning back | about 190 mm | about 31 deg |

At S5 the tap meets the skin at about 31 degrees: for every 1 mm it moves
down it moves about 1.7 mm along the finger. That is the drift. The S4/S5
distal pulses (980-1110 us) also sit near the EMAX's 900 us floor, where it
is least reliable. These numbers are estimates. Check them on a side photo
(next point) before acting on them.

**What to do, in order:**
1. **Measure.** Take a side photo square to the arm at S1, S3 and S5, with a
   ruler in the picture. Measure (a) how far the finger's back is below the
   proximal shaft, and (b) the angle between the tube and the skin. Send the
   photos or the numbers.
2. **Raise the hand (decided 2026-11-26) and centre the sites, measured with
   the arm itself.** Chancel et al. do not publish the height of the arm above
   the hand (PROJECT.md 1.2: "not stated"), and their links differ from ours,
   so the amount is set on our rig. The target: at S3 the proximal is about
   10 degrees below level (about 1720 us) and the distal hangs straight down.
   Not higher: RAISED = CONTACT + S must stay at or below level (1830 us), so
   the highest CONTACT can be 1730 us with S = 100 us.
   1. In `arm_jog`, set the proximal to 1720 us.
   2. Move the distal until the tube hangs vertical (check with a set square
      or a plumb line).
   3. Slide the hand along the groove until the S3 dot is right under the
      tube.
   4. Measure the gap between the end of the tube and the S3 dot. Raise the
      hand by that gap.
   From the estimate above this is roughly 3-4 cm up and about 4 cm further
   from the base, but the measurement decides. With this pose the tap meets
   the skin at about 50 degrees instead of 31, and S1 and S5 are about
   13 degrees either side of vertical instead of S1 near vertical and S5
   20 degrees past it.
3. **Refit the distal horns at the same time.** In the estimate, the tube
   hangs vertical near 1000 us (top) and 960 us (down), close to the EMAX's
   900 us floor. After the hand is raised the sites would then fall around
   and below the floor. Refit each distal horn so that the tube hangs
   vertical at about 1450 us, the middle of the EMAX's range: send
   `top dist 1450` (or `down dist 1450`), loosen the horn screw, refit the
   link hanging vertical (as near as the spline allows), and tighten. The
   five sites then fall about 1330-1570 us, away from both range ends and
   from 1200 us. This also replaces the distal start pulse proposal of
   issue 2. The distal ZERO and its limits in `geometry.md`, `arm_jog` and
   `arm_taps` then change too.
4. After moving anything, teach both hands again
   (`site-calibration.md` sections 4-5).
5. **Longer term, needs a decision.** A tap that comes straight down needs the
   tip level with the proximal shaft. With the distal hanging down, that
   means raising the proximal about 40 degrees above level, which the "never
   above level" ceiling forbids today. Discuss before changing the ceiling:
   it is a safety rule, and it changes the drawings in PROJECT.md 2.2 and 2.3.

---

## Issue 2: HOME sometimes does not bring a joint back

**Seen (2026-11-26, `arm_taps`).** After `HOME`, sometimes the proximal does
not return to its start pose, and sometimes the distal does not.

**Status.** Open. **Evidence (2026-11-26):** `SHOW` sometimes reports the
start pulses after HOME while the arm is visibly not there. The command was
sent and the servo did not follow, so the cause is in the hardware (power,
signal, or the servo at that pulse), not in the program.

**Software checked.** `do_home()` in `arm_taps` sets both targets to ZERO and
the joints slew there at 600 us/s (proximal) and 1000 us/s (distal). No path
was found in which a joint is left without a target. While a run is going,
HOME is refused with `ERR a run is going; type STOP first`. If that line
appeared, the arm was never asked to move.

**Probable causes:**
- Power or signal (section 0).
- **Distal: the start pulse is at the end of the EMAX's range.** The distal
  ZERO is 1900 us (top) and 1860 us (down), at the top of the 900-1900 us
  operating range. Step 3.1 found the EMAX unreliable near its ends ("after
  being sent to an end it did not answer a step 50-100 us back"). The taught
  AIM pulses never go above 1265 us, so the distal never needs to go that
  high.
- **Proximal: level is the most heavily loaded pose.** The arm sticks out
  furthest when it is level, so holding it there takes the most torque. A
  servo that is short of current, or that is near its dead band, can stop a
  few us short (geometry.md: no visible motion for 10-20 us steps below
  level).

**What to do:**
1. Section 0 first.
2. After HOME, type `SHOW`. If it shows `prox=1830 dist=1900` (or `1860`)
   while the arm is visibly elsewhere, the command was sent and the servo did
   not follow (hardware). If `SHOW` shows other numbers, it is software: send
   me the output.
3. **Move the distal start pulse away from the range end.** This is done
   together with the horn refit of issue 1, point 3: once the tube hangs
   vertical at about 1450 us, the distal start pulse becomes about 1450 us
   too, in the middle of the EMAX's range. Check by hand in `arm_jog` that the
   tip clears the hand at that pose with the proximal level.

---

## Issue 3: the EMAX buzzes, shakes, and sometimes keeps shaking

**Seen (2026-11-26).** The EMAX buzzes and shakes, and sometimes does not
stop shaking until it is given another command.

**Status.** Open.

**Probable causes:**
- Power or signal (section 0). A misread pulse width makes a digital servo
  chase a target that keeps changing.
- **Hunting under load.** Step 3.1 measured the bare servo (horn only). On the
  arm it carries the distal link and the tube, and it has plastic gears with
  play. Near some positions the control loop overshoots back and forth across
  that play and does not settle. A new command moves it to a different
  position, where it may settle, which matches "stops when I give another
  command".
- Known weak spots: 1200 us (slight shaking, Step 3.1), and the ends of the
  range (issue 2).
- **During contact:** at S4/S5 the slanted tap (issue 1) pushes the tip along
  the finger, and the distal fights the push.

**What to do:**
1. Section 0 first.
2. Note **when** it shakes: in free air or only in contact; at which pulse;
   while moving or while holding. Use the laser pointer to see how large the
   shake is.
3. **Proposed software change (needs your OK):** finish every distal move
   from the same side, a little beyond and then back (for example 30 us), so
   the gears always end against the same side of their play. This also helps
   issue 4.
4. Lighten what the distal carries: a shorter or lighter tube.
5. Under load, compare 50 Hz with 100 Hz (Step 3.1 compared rates with the
   horn only).
6. **Last resort, needs a decision:** replace the ES9257 with a metal-gear
   digital servo of similar size. Now that the distal decides where every
   site lands, its accuracy matters more than PROJECT.md 2.2 assumed. This
   changes deviation D2.

---

## Issue 4: small steps (5 or 10 us) do not move at once, then move all together

**Seen (2026-11-26, `arm_jog` key mode).** With a 10 us step the joint
sometimes does not move, and neither does it after a second step; then it
moves. The program still stores each pulse. It happens less with 30 us steps.

**Status.** Open.

**Probable cause: dead band and friction under load.** The servo moves only
when its position error is larger than its own threshold. Under the arm's
weight and the gear friction that threshold is larger than the PDI's
published 2 us: on 2026-11-24 no motion was seen for 10 and 20 us steps,
only for 30 us (`geometry.md`). Small steps add up in the software until the
total is large enough, and then the servo jumps. So the pulse the program
stores is the commanded pulse, and the arm may not yet be there.

**Why it matters.** CONTACT is recorded as the commanded pulse. If the arm
lags by one or two small steps, the stored CONTACT can be 10-20 us off
(about 3-6 mm at the tip), and its error depends on how you approached.

**What to do:**
1. Section 0 first (a cleaner signal can narrow the dead band).
2. **Procedure (no code change).** Before pressing `c`, always do the check
   from above (`site-calibration.md` section 5, step 4): `u` to lift, `b` to
   come back down. The `b` descent is a large move, like the real tap, so the
   servo really goes to the pulse that will be stored. Do not use 5 us steps;
   10 us is the smallest useful one.
3. **Proposed software change (needs your OK):** in `arm_jog` key mode,
   finish every proximal step by approaching from a fixed height above (for
   example go to the target + 40 us, then down to the target). Then even a
   5 us step ends in a real move from above, the same direction as the tap.
   Same idea for the distal (issue 3, point 3).
4. Measure the real dead band under load with the laser pointer: from a fixed
   pose, steps of 5, 10, 20 and 30 us, down and then up. Record it in
   `geometry.md`.
