# Bill of materials

Working copy. See PROJECT.md part 3 for the full annotated list with reasons.
Tick items as they arrive and record what you actually bought - substitutions
matter for reproducibility.

## Have

- [x] Teensy 4.1
- [x] JX PDI-4409MG x2
- [x] EMAX ES9257 x2
- [x] PCA9685
- [x] Acer C120 projector

## Electronics

- [ ] 5 V / 6 A PSU or UBEC
- [ ] 74AHCT125 quad level shifter x2 - buy SN74AHCT125N (TI, DIP-14), lot of 10
      (AliExpress item 1005007655828920). NOT 74AHC125 / 74HC125: without the
      T, 3.3 V input is not a reliable HIGH at 5 V supply. 74HCT125N is an
      acceptable substitute (update PROJECT.md section 2.7 if used).
      Arrival test, at least 2 chips from the lot, before any servo is wired:
      check the marking (SN74AHCT125N, TI logo); then VCC pin 14 to 5 V,
      GND pin 7, /OE pins 1, 4, 10, 13 to GND, Teensy pin into 1A (pin 2):
      1Y (pin 3) must read ~0 V for LOW and ~5 V for HIGH, and on the logic
      analyser its pulse width must match the input.
- [ ] 1000 uF 16 V low-ESR capacitor x2
- [ ] 100 nF ceramic x10
- [ ] BAT85 Schottky diode x10
- [x] 1 Mohm resistor x10
- [ ] Resistor assortment
- [x] Breadboard + jumpers
- [ ] Perfboard x2
- [ ] Servo extension leads x4
- [ ] Screw terminals x4

## Contact sensing (buy both, choose after step 3.4)

- [ ] Piezo disc 20-27 mm x5
- [x] FSR 402 x2 - bought: FSR402, 0.5 inch round sensing area
- [ ] Mounting adhesive

## Measurement

- [x] 8-channel USB logic analyser        <- most important single purchase
      bought: WeAct DLA Mini, 8 channels, 24 MHz (one sample every ~0.04 us)
- [ ] Digital multimeter
- [x] Photodiode BPW34 or TEPT4400 + 10 kohm - bought: TEPT4400 x10
      (3 mm, peak 570 nm). The 10 kohm comes from the resistor assortment,
      not bought yet.
- [ ] Microphone (lavalier or measurement)
- [ ] Kitchen scale, 0.1 g
- [ ] Digital calipers
- [ ] Lux meter (or calibrated phone app)
- [ ] Oscilloscope (optional)
- [ ] Bench PSU with current limit (optional)

## Mechanical

- [x] Aluminium flat bar 3 cm x 2-3 mm, 1 m
- [x] Base plates 10 x 20 cm x2
- [x] Silicone tube 7 mm OD - bought: transparent food-grade silicone tube
      (size to be confirmed: ID x OD)
- [x] M3 hardware, bearings, standoffs
- [x] Table clamps x4

## Participant apparatus

- [x] Cosmetic prosthetic right hand
- [ ] Chin rest
- [ ] Elbow rest
- [ ] Closed-back headphones
- [x] Black cloth

## Visual noise: beam splitter + monitor (replaces the projector, to be written
## up as deviation D5 in PROJECT.md section 1.8)

Plan: a laptop-driven monitor above a half-silvered plate; the participant
sees the rubber hand through the plate and the noise reflected in it. The
Acer C120 projector stays as a fallback. Ordered 2026-11-22, delivery
Thursday, so none ticked yet. Tick on arrival and measure the real values.

- [ ] Clear cast acrylic plate, 3 mm, A3 (297 x 420 mm) - ordered: Langaelex
      "Plaque Acrylique Transparente 3mm A3", cast PMMA, EUR 13.99. Substrate
      for the mirror film. Keep the protective film on until applying.
- [ ] Silver one-way window mirror film - ordered: rabbitgoo "Film Miroir pour
      Fenetre", colour Argente, 90 x 199.5 cm, EUR 25.64. Listing states 26%
      visible light transmission; reflectance not stated. Apply wet, silver
      side up (toward the screen and the eye).
- [ ] Portable monitor 15.6" IPS 1080p - ordered: CUIUIC (seller XZXEU-FR),
      mini HDMI + 2x USB-C, EUR 59.99. Listing claims 400 cd/m2; measure the
      real value. Turn off HDR, eye-care mode and FreeSync when used.
- [ ] Mini HDMI to HDMI cable - not bought yet (Dell has a full-size HDMI
      port; check if the monitor includes one)
- [ ] 5 V / 2 A USB charger for the monitor's power-only USB-C port - not
      bought yet, unless the monitor box includes one
- [ ] Clip-on red/blue (anaglyph) 3D glasses - ordered: PRECORN, EUR 9.99.
      Purpose to be confirmed; not part of the beam-splitter plan so far.
- [ ] Squeegee and spray bottle for the film - check whether the film kit
      includes them

Measurements to record on arrival (docs, with the lux app): monitor
brightness; film transmission and reflection once applied to the plate.
