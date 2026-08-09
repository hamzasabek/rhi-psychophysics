# Working agreement

`PROJECT.md` in this directory is the specification, hardware reference and
roadmap for this project. Read it before proposing any change. If a decision
in it turns out to be wrong, update the document as part of the same change.

## Hardware

- Teensy 4.1 (3.3 V logic, pins are NOT 5 V tolerant)
- 2x JX PDI-4409MG on the proximal joints - these MAKE the tap, timing-critical
- 2x EMAX ES9257 on the distal joints - these SELECT the site, not timing-critical
- Acer C120 pico projector for visual noise (USB display, 854x480)
- PCA9685 reserved for non-timing-critical outputs only

## Non-negotiable conventions

1. **Comment every line.** This codebase is a teaching artefact. Every function
   gets a block comment explaining what and why, with a worked numeric example
   wherever arithmetic is involved. Every non-obvious line gets a line comment.
   Assume nothing is obvious.

2. **No `delay()` in firmware. Ever.** Use `elapsedMillis` / `elapsedMicros`
   and the event queue.

3. **No heap allocation after `setup()`.** No `new`, no `malloc`, no
   `std::vector`, no `std::string`, no Arduino `String`.

4. **Never compare timestamps directly.** `micros()` wraps every ~71.6 minutes.
   Always use `clk::is_due()`.

5. **Sign convention:** `t_rubber = t_real + delta`. Positive delta means the
   REAL hand is touched first. Restate this in any code that touches it.

6. **Layers point one way:** TrialRunner -> RobotArm -> ServoDriver -> hardware.
   Never upward.

7. **Explain before implementing.** Describe the concept and the reasoning
   first, then write the code. Ask if something is ambiguous rather than
   guessing.

## Git

- Conventional Commits: `feat(scope): ...`, `fix:`, `test:`, `docs:`,
  `refactor:`, `chore:`
- Commit messages describe the change and nothing else. No references to
  tooling, no co-author trailers, no mention of how the code was produced.
- Tags are version numbers only: `v0.1.0`, `v0.2.0`, ...
- One roadmap step, one commit.

## Verify before claiming done

- Firmware logic: `cd firmware && pio test -e native`
- Firmware build: `cd firmware && pio run -e teensy41`
- Host: `pytest host/tests && ruff check host`
