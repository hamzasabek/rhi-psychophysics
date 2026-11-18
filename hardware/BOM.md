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
- [ ] 1 Mohm resistor x10
- [ ] Resistor assortment
- [ ] Breadboard + jumpers
- [ ] Perfboard x2
- [ ] Servo extension leads x4
- [ ] Screw terminals x4

## Contact sensing (buy both, choose after step 3.4)

- [ ] Piezo disc 20-27 mm x5
- [ ] FSR 402 x2
- [ ] Mounting adhesive

## Measurement

- [ ] 8-channel USB logic analyser        <- most important single purchase
- [ ] Digital multimeter
- [ ] Photodiode BPW34 or TEPT4400 + 10 kohm
- [ ] Microphone (lavalier or measurement)
- [ ] Kitchen scale, 0.1 g
- [ ] Digital calipers
- [ ] Lux meter (or calibrated phone app)
- [ ] Oscilloscope (optional)
- [ ] Bench PSU with current limit (optional)

## Mechanical

- [ ] Aluminium flat bar 3 cm x 2-3 mm, 1 m
- [ ] Base plates 10 x 20 cm x2
- [ ] Silicone tube 7 mm OD
- [ ] M3 hardware, bearings, standoffs
- [ ] Table clamps x4

## Participant apparatus

- [ ] Cosmetic prosthetic right hand
- [ ] Chin rest
- [ ] Elbow rest
- [ ] Closed-back headphones
- [ ] Black cloth
