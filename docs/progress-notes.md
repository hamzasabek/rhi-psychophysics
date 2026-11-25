# Progress notes — what's built so far, explained

This is a companion to `PROJECT.md`, not a replacement for it. `PROJECT.md` is
the spec, the hardware reference, and the roadmap — it stays the single source
of truth. This document is a snapshot: a plain-language walk through every
file that exists in the repository right now, what it does, and why it's
built the way it is. Read it top to bottom once, then use it as a map back
into the code.

Last updated: 2026-11-23. Groups 1 and 2 are complete (`v0.2.0`); Step 3.1
(servo characterisation) is done and merged; Step 3.2 (arm geometry) has
started. §7 says exactly where to resume.

---

## 1. The one-sentence version of the project

Two small robot arms tap a participant's real, hidden index finger and a
visible rubber index finger with a controllable delay between the two touches
(down to the millisecond), so we can measure how much delay it takes before
the "this rubber hand feels like my own hand" illusion breaks. Everything in
the firmware exists to make that delay a *known, precisely-measured* number
instead of a hopeful guess.

## 2. The architecture, and why it's split this way

### 2.1 Two computers, two jobs

```
Host PC (Python)  --USB serial, plain text-->  Teensy 4.1 (C++)  --wires-->  servos, sensors
   decides                                         executes & measures
```

**Python decides. C++ executes and measures.** This is not a style
preference — it's forced by physics. A desktop OS (Windows/macOS/Linux) can
pause any program for a few milliseconds at any moment — a disk flushed, a
browser tab woke up — and there is nothing you can do about it from inside
the program. That's fine for choosing *which* condition to run next, but
fatal for measuring a tap that has to land within about ±15 ms. The Teensy
has no operating system: nothing runs on it except the code we wrote, so its
microsecond clock (`micros()`) is trustworthy in a way no desktop clock ever
is. So: the host owns experiment design, randomisation, audio, the projected
visual noise, and every file written to disk. The Teensy owns the clock, the
scheduling, the physical movements, and the contact timestamps. If the
firmware crashes mid-session, you lose the current trial and nothing else,
because the host holds everything else.

### 2.2 The firmware's internal layer stack

PROJECT.md §4.1 lays out the intended final shape:

```
HostLink        parse commands from the host, emit events back to it
TrialRunner     expand one trial description into a list of timed events
EventQueue      a timestamped priority queue — "what's next, and when"
RobotArm ×2     select a site, then tap, then lift
ServoDriver     turn an angle into a pulse width, own the actual pins
ContactSensor   timestamp the moment contact is detected
Clock           the one safe way to ask "has this time arrived?"
```

**Dependencies only point downward** — `TrialRunner → RobotArm → ServoDriver
→ hardware` — and never the other way. A `ServoDriver` must never know that
`TrialRunner` exists. This is what lets each layer be tested alone, and it's
why, right now, `Clock.h` and `EventQueue.h` have zero dependency on anything
Teensy-specific (see §4.2–4.3): they're pure logic, so they can run on a
laptop with no hardware attached.

**What exists today, of that stack:** `Clock`, `EventQueue`, and `HostLink`.
`TrialRunner`, `RobotArm`, `ServoDriver`, and `ContactSensor` are not written
yet — they're the next few roadmap groups. `main.cpp` currently only wires up
`HostLink`; nothing schedules or moves anything yet.

## 3. Repository layout, annotated

```
rhi-psychophysics/
├── PROJECT.md                 the spec / hardware reference / roadmap — read first
├── CLAUDE.md                  short pointer file + non-negotiable conventions for Claude Code
├── README.md                  human-facing quick start (clone, build, test)
├── rhi.code-workspace         open THIS in VSCode — gives PlatformIO and Python separate roots
│
├── firmware/                  PlatformIO project — everything that runs on the Teensy
│   ├── platformio.ini         defines the teensy41 (real board) and native (laptop test) builds
│   ├── src/
│   │   ├── main.cpp           setup()/loop() — currently just drives HostLink
│   │   ├── Clock.h            rollover-safe microsecond time (Step 2.1)
│   │   ├── EventQueue.h       fixed-capacity min-heap of timed actions (Step 2.2)
│   │   ├── protocol.h         the serial message contract, host and firmware both obey it
│   │   ├── HostLink.h/.cpp    reads USB serial, parses lines, replies (Step 2.3)
│   │   └── (RobotArm, ServoDriver, ContactSensor, TrialRunner — not written yet)
│   ├── test/                  native (laptop) unit tests, no hardware needed
│   │   ├── test_sanity/       proves the test harness itself works
│   │   ├── test_clock/        proves clk::is_due() survives the 32-bit rollover
│   │   └── test_event_queue/  proves the heap always pops the earliest event
│   └── experiments/
│       ├── servo_char/        throwaway test program for Step 3.1 (its own PlatformIO project)
│       └── arm_jog/           throwaway test program for Step 3.2 (its own PlatformIO project)
│
├── host/                      the Python package — everything that runs on the PC
│   ├── pyproject.toml         dependencies (pyserial, numpy, pandas, sounddevice, pygame...)
│   ├── rhi/__init__.py        package init: a version string + a design note
│   ├── rhi/protocol.py        Python twin of protocol.h: builds PING, reads PONG/ERR
│   ├── rhi/link.py            the serial conversation + host/Teensy clock sync (Step 2.4)
│   └── tests/                 test_sanity, test_protocol, test_link — no hardware needed
│
├── docs/
│   ├── notes.md                how to create/activate the Python venv on this machine
│   ├── geometry.md              template to fill in during Step 3.2 (measured arm geometry)
│   ├── servo-characterisation.md  being filled in during Step 3.1 (measured servo specs)
│   ├── timing-notes.md          running log of every timing measurement (starts at Step 2.4)
│   ├── progress-notes.md        this file
│   └── figures/                 photos/diagrams (arm-geometry-side.png etc.)
│
├── hardware/BOM.md             shopping-list working copy of PROJECT.md's bill of materials
└── data/{raw,calibration}/     empty on purpose — participant data is gitignored, not committed
```

Files under `docs/` marked "template to fill in" are intentionally mostly
blank right now — they're placeholders for measurements that require
hardware we don't have assembled yet (Steps 3.1 and 3.2).

---

## 4. Firmware, file by file

### 4.1 `platformio.ini` — two builds from one source tree

Two environments are declared:

- **`[env:teensy41]`** — compiles for the real board (`platform = teensy`,
  `board = teensy41`, `framework = arduino`), with `-Wall -Wextra
  -Werror=return-type` so warnings (like a function that forgets to return a
  value on some path) become build failures instead of silent bugs.
- **`[env:native]`** — compiles the *same source files* for your own laptop
  (`platform = native`), with a `-DUNIT_TEST` flag defined. That flag is what
  lets `Clock.h` (see below) swap out the real Teensy `micros()` for a fake,
  test-controlled clock, so pure-logic bugs can be caught in seconds without
  ever touching hardware.

### 4.2 `Clock.h` — the rollover problem, solved once

**The problem.** The Teensy's `micros()` counts microseconds since power-on
into a 32-bit number. 32 bits maxes out at 4,294,967,295, so the counter
wraps back to zero after about 71.6 minutes. A recording session runs longer
than that, so at some point during every session, `micros()` will suddenly
become small again while real time keeps moving forward. Comparing two
timestamps the naive way (`now >= scheduled_time`) gives the *wrong* answer
for a window around every wrap.

**The fix — `clk::is_due(t, now)`.** Instead of comparing the two numbers
directly, it subtracts them first (`now - t`), then reads the *same bits* as
a **signed** number instead of unsigned, and checks whether that signed value
is `>= 0`.

Why this works, worked through with real numbers: unsigned subtraction in
C++ silently wraps too, so subtracting across a rollover still gives the
*true* elapsed time:

```
t   = 4294967290     (10 microseconds before the counter's maximum)
now =         10     (counter has wrapped; 16 microseconds have truly passed)

now - t  =  10 - 4294967290
         =  -4294967280                (ordinary arithmetic)
         =  -4294967280 + 4294967296   (unsigned wraps: adds 2^32)
         =            16               <- correct elapsed time
```

Reading those same 32 bits as *signed* rather than *unsigned* is what turns
"16" into a small positive number (due) and turns a genuinely-still-in-the-
future timestamp into a huge unsigned value that, read as signed, comes out
negative (not due). One line of code, `(int32_t)(now - t) >= 0`, handles the
ordinary case, the future case, and the rollover case identically — no
special-casing needed anywhere else in the program. This is valid as long as
the true gap between the two times is under about 35.8 minutes, which is
enormously more than anything this project ever waits for (the longest wait
anywhere is a 2-second gap between taps).

**`clk::before(a, b)`** is the same trick used to answer a slightly different
question — "does `a` come strictly before `b`?" — needed by the event queue
to decide ordering. It must be used instead of a plain `<` anywhere two
timestamps are compared, for exactly the same rollover reason.

**The test/production split.** Under `#ifdef UNIT_TEST`, `now_us()` returns a
plain global variable (`g_fake_micros`) that tests set directly to any value
they like — including values one microsecond away from the wrap, which would
be impractical to actually wait for on real hardware. In the real Teensy
build, that branch doesn't exist and `now_us()` calls the genuine
`micros()`. Both builds share every other line of the file.

### 4.3 `EventQueue.h` — "what's next, and when", cheaply

**The problem it solves.** A single trial needs roughly 25 separate timed
actions (for each of 6 taps × 2 arms: a "move to the site" and a "make
contact"), and their order changes with every asynchrony condition. Rather
than a tangle of `if` statements trying to track "what should be happening
right now," the firmware will build a list of `(time, action)` pairs up
front, and the main loop becomes one repeated question: **is the earliest
thing in the list due yet?**

**Why a binary min-heap and not a sorted list.** A heap is a plain
fixed-size array with one rule enforced on it: every parent slot holds an
earlier time than either of its two children, where "parent" and "child" are
found by pure index arithmetic (child indices `2i+1`, `2i+2`; parent index
`(i-1)/2` — no pointers, nothing allocated). That rule is *weaker* than
"fully sorted," which is exactly why it's cheap: inserting or removing an
item only has to fix the one broken chain from root to leaf (`sift_up` /
`sift_down`), not re-sort everything. The one guarantee it gives — the
earliest event is always at index 0 — is the only guarantee the firmware
actually needs.

**`push(e)`** drops the new event in the next free array slot, then calls
`sift_up`: repeatedly swap it with its parent while it's earlier than that
parent. Worked example from the code comments — pushing time `150` into a
heap that currently has `700` at index 3, `300` at index 1, `100` at index
0: `150` swaps past `700`, then past `300`, then stops at `100` because `150`
is not earlier than `100`. Three comparisons, not a full re-sort.

**`pop()`** takes the item at index 0 (guaranteed earliest), moves the very
last item in the array up to index 0 to fill the hole, then calls
`sift_down`: repeatedly swap that displaced item with whichever of its two
children is earlier, until neither child is earlier than it.

**Fixed capacity, no allocation.** `EventQueue<Capacity>` is a *template* —
`EventQueue<64>` and `EventQueue<4>` are two different classes generated at
compile time, each with its own fixed-size array baked in. There is no
`new`, no heap growth, ever. `push()` on a full queue returns `false` instead
of overflowing the array, and the caller is expected to check that — per
CLAUDE.md's philosophy, failing loudly and immediately beats corrupting
memory silently and finding out three hours into a session.

**Never compares timestamps with `<`.** The internal `earlier(a, b)` helper
calls `clk::before()`, never a raw `<`, for the same rollover reason as
`Clock.h` above — two events queued near opposite sides of a wrap must still
come out in true time order.

**What the tests prove** (`test_event_queue.cpp`): a fresh queue starts
empty; push-then-pop hands back exactly what went in; five events pushed in
scrambled order come out in ascending time order; a queue of capacity 2
refuses a third push without growing; popping an empty queue returns a
harmless `EventType::None` event rather than crashing; ordering is preserved
across a rollover boundary; and a 200-event stress test (scrambled with a
small hand-written pseudo-random generator, chosen specifically because it's
*deterministic* — a flaky test that fails once in a hundred runs is worse
than useless) comes out fully ordered.

### 4.4 `protocol.h` — the contract, not the code

This file defines *what the messages mean*, in comments, and deliberately
contains no logic at all. Right now it documents exactly one round trip:

```
Host  -> Teensy:  PING <seq>
Teensy -> Host:   PONG <seq> t_us=<n>
Teensy -> Host:   ERR <msg>          (on anything unrecognised)
```

Plain ASCII text, one message per line, was chosen specifically so a human
can type these commands directly into a serial monitor while debugging — no
special tooling required to poke at the firmware. The intent (stated in the
file and in PROJECT.md §4.6) is that every future command gets written here
*first*, as a contract, and only then implemented — and the Python host
mirrors this exact file in `rhi/protocol.py` (Step 2.4, see §5), so both
sides of the USB cable are provably talking about the same grammar.

### 4.5 `HostLink.h` / `HostLink.cpp` — talking to the host without ever blocking

**The header (`HostLink.h`)** is the "front panel": it declares a class with
exactly two public methods —

- `begin()` — call once from `setup()`, opens the USB serial port.
- `poll()` — call on every single pass of `loop()`; reads whatever
  characters are waiting (possibly zero), and never waits for more to
  arrive. This non-blocking shape is what CLAUDE.md's "no `delay()`, ever"
  rule looks like in practice for serial I/O.

Everything else — the character buffer, how much of it is filled, the
overflow flag — is `private`. Nothing outside the class can reach in and
corrupt that state; the only way to interact with a `HostLink` is through
its two declared methods. That's the "narrow interface" principle from
PROJECT.md §0.5 applied to the smallest possible module.

**The implementation (`HostLink.cpp`) has gone through three layers so
far**, visible in the file's own history:

- **Layer 1** (already committed): recognise `PING <seq>`, reply `PONG
  <seq> t_us=<clock>`.
- **Layer 2** (already committed): the non-blocking collect-until-`\n`
  loop, and stripping `\r` so Windows line endings don't confuse anything.
- **Layer 3** (committed in `0d05d99`): harden the parser against three
  ways real-world input goes wrong.

Walking through `poll()` as it stands today: it loops while
`Serial.available() > 0`, reading one character at a time.

- **`\r`** is silently dropped — Windows sends `\r\n` for a single Enter
  keypress, and stripping the `\r` here means nothing downstream ever has to
  think about it.
- **An ordinary character** is stored in `buffer_[length_]` and `length_` is
  incremented — *if there's room*. `BUFFER_SIZE` is 128, but only indices
  0–126 are ever used for content: index 127 is permanently reserved for the
  C-string terminator (`'\0'`), so the room check is `length_ < BUFFER_SIZE -
  1`. If the buffer is already full and another character arrives, it can't
  be stored without corrupting that reserved terminator slot — so instead an
  `overflow_` flag is set, and the character is simply discarded. Every
  further character before the next `\n` hits the same branch and re-sets
  the same flag, which is harmless.
- **`\n`** means "line complete," and there are now three distinct outcomes,
  checked in priority order:
  1. **`overflow_` is set** → the line was too long. Print `ERR line too
     long, max 127 chars`, then clear both `overflow_` and `length_`.
     Crucially, `handle_line()` is **never called** in this case — the
     buffer only holds the *first* 127 characters of whatever was actually
     sent, which could easily look like the start of a real command by pure
     coincidence, so parsing that fragment would be worse than refusing it
     outright.
  2. **`length_ == 0`** → an empty line. This happens routinely (a bare
     Enter press, or the leftover `\n` half of a `\r\n` pair after the `\r`
     was already stripped) and is not an error — it's silently ignored.
  3. **Otherwise** → a normal, complete line. Terminate it with `'\0'` and
     hand it to `handle_line()`.

`handle_line(const char* line)` is where the actual command grammar lives.
It runs `strncmp(line, "PING ", 5)` — comparing five characters including
the trailing space, so a message like `"PINGWHATEVER"` (verb with no
separator) does *not* accidentally match. C strings can't be compared with
`==` (that would compare two memory addresses, not the text), so `strncmp`
walks the characters one at a time. If it matches, `line + 5` is pointer
arithmetic meaning "the address five bytes further along" — i.e. wherever
the sequence number starts — and that text is echoed back verbatim inside
the `PONG` reply, alongside the current clock reading. If the line is
exactly `"PING"` with nothing after it — checked with `strcmp`, a *full*
comparison including the terminator, so `"PING2"` or `"PING-X"` correctly
fall through instead of matching — the firmware replies with a specific,
actionable error (`ERR PING needs a sequence number, e.g. PING 1`) rather
than the generic catch-all. Anything else at all falls through to `ERR
unknown command: <whatever was sent>`. In every one of these cases the
Teensy keeps running — it never gets into a state where malformed input can
wedge or crash it, which matters because a USB glitch 180 trials into a
session is not a hypothetical.

### 4.6 `main.cpp` — the whole program, for now

```cpp
HostLink host;
void setup() { host.begin(); }
void loop()  { host.poll();  }
```

That's genuinely all of it at this stage: one global `HostLink` object,
opened once, polled forever. This is deliberately the smallest possible
`main.cpp` that exercises everything built so far — it will grow a
`TrialRunner`, an `EventQueue`, and two `RobotArm`s as those pieces get
built, but nothing has been added ahead of when it's needed.

### 4.7 The native tests

All three test files live under `firmware/test/` and run with `cd firmware
&& pio test -e native` — compiled for your laptop, no Teensy required,
finishing in seconds.

- **`test_sanity.cpp`** asserts `2 + 2 == 4`. Its only job is to prove the
  test harness itself is wired up correctly, so that if a *real* test later
  fails to even run, you know the problem is your setup, not your code.
- **`test_clock.cpp`** exercises `clk::is_due()` five ways: the ordinary
  past case, the ordinary future case, the exact-equality boundary (must
  count as due — otherwise an event scheduled for the *exact* microsecond a
  loop iteration checks would be skipped entirely), and both directions of
  the rollover boundary described in §4.2.
- **`test_event_queue.cpp`** exercises the heap as described in §4.3:
  empty-queue behaviour, push/pop round-trip, out-of-order-in →
  ordered-out, full-queue refusal, safe pop-when-empty, `peek_due`
  respecting the clock, ordering across a rollover, and the 200-event
  deterministic stress test.

Each test file sets up `setUp()`/`tearDown()` hooks (Unity's equivalent of
"run this before/after every test") so that, for example, the fake clock
always starts at zero for `test_clock.cpp` regardless of what an earlier
test in the same run left it at.

---

## 5. Host (Python), file by file

Group 1 built the skeleton; Step 2.4 added the serial link and clock sync.

- **`pyproject.toml`** declares the package name (`rhi`), Python ≥3.10, and
  the dependencies each future piece will need: `pyserial` (talk to the
  Teensy), `numpy`/`pandas` (randomisation and logging), `sounddevice`
  (masking noise and cue tones), `pygame` (the projected visual noise
  field), `matplotlib`/`scipy` (later analysis). Dev-only tools (`pytest`,
  `ruff`) are a separate `[project.optional-dependencies] dev` group so a
  deployed rig doesn't need to install a test framework. `ruff` is
  configured for an 88-character line length and a specific lint rule set
  (`E, F, I, UP, B` — pyflakes errors, unused imports, import sorting,
  "modernise this syntax," and common bug patterns).
- **`rhi/__init__.py`** only records `__version__ = "0.2.0"` and,
  in its module docstring, restates the responsibility boundary from §2.1 —
  a reminder, right at the package's front door, that timing decisions
  belong to the firmware, not here.
- **`tests/test_sanity.py`** just imports `rhi` and checks `__version__`
  looks like a version string. Its real purpose, per its own docstring, is
  to fail loudly on the single most common first-day mistake: forgetting to
  run `pip install -e "host[dev]"` (an *editable* install, meaning the
  installed package **is** your source folder — edit a file, the change is
  live immediately, no reinstall step to forget).
- **`rhi/protocol.py`** is the Python twin of `protocol.h`. `format_ping(7)`
  builds `"PING 7\n"`; `parse_line()` turns a received line into a
  `Pong(seq, t_us)` or an `Err(msg)`, and raises `ProtocolError` for
  anything else (including a `t_us` too big for 32 bits) instead of
  guessing. It holds no state and touches no port, so it is tested with
  plain strings copied from real firmware output.
- **`rhi/link.py`** is the conversation over the cable, plus clock sync.
  `open("COM3")` sends a bare `\n` first, to end any half-typed line left
  on the Teensy. `Link.ping()` reads the host clock (`perf_counter_ns`)
  just before sending and just after the reply arrives; the Teensy's
  timestamp lies somewhere between the two. `sync_clock(n=20)` keeps the
  *fastest* of 20 round trips, because that one was delayed least by the
  OS, and returns `teensy_us = host_us + offset_us`, ± half that round
  trip. The arithmetic (`estimate_offset`, `offsets_agree`, `drift_ppm`,
  and `wrap_diff_us`, which is `Clock.h`'s rollover trick in Python) is
  kept as pure functions, so tests feed it hand-made numbers. Command
  line: `python -m rhi.link --port COM3 --sync --repeat 3`.
- **`tests/test_protocol.py`, `tests/test_link.py`** test all of the above
  with nothing plugged in: a `FakeTeensy` stands in for the serial port
  and a scripted fake clock stands in for `perf_counter_ns`.

---

## 6. Conventions you'll see enforced everywhere in this code

These come from `CLAUDE.md` and `PROJECT.md` §0.3–0.5, and every file above
follows them — worth naming explicitly so the pattern is recognisable
elsewhere:

- **Every line commented, every function given a worked numeric example
  where arithmetic is involved.** See the rollover walkthrough in `Clock.h`,
  the sift-up walkthrough in `EventQueue.h`, the overflow walkthrough in
  `HostLink.cpp`.
- **No `delay()`, anywhere, ever.** `HostLink::poll()` is written to return
  immediately when nothing is waiting, specifically so it can be called
  every pass of `loop()` without ever stalling other work.
- **No heap allocation after `setup()`.** `EventQueue` is a compile-time
  template over a fixed array; `HostLink`'s line buffer is a fixed
  `char[128]`. No `new`, `std::vector`, `std::string`, or Arduino `String`
  appears anywhere in `firmware/src/`.
- **Never compare raw timestamps.** Every single time comparison in the
  firmware goes through `clk::is_due()` or `clk::before()` — never a bare
  `<`, `>`, or `>=` on two `uint32_t` clock readings.
- **Layers point one way.** `EventQueue` doesn't know `HostLink` exists;
  `Clock` doesn't know either of them exists. Each file's `#include` list
  only reaches downward.
- **Fail loudly, not silently.** `EventQueue::push()` returns `false`
  rather than overflow; `HostLink` reports `ERR` rather than silently
  dropping or misparsing garbage; compiler warnings are build errors
  (`-Werror=return-type`).

---

## 7. Where things stand right now, exactly

Git history so far, mapped to the roadmap in `PROJECT.md` Part 5:

| Commit | Roadmap step |
|---|---|
| `edfd9d2` | Step 1.1 — repository skeleton |
| `07b3e54` | Step 1.2 — PlatformIO project, non-blocking blink |
| `ab30872` | Step 1.3 — Python package skeleton |
| `39bb837` | Step 1.4 — native + pytest test harnesses (`v0.1.0`) |
| `f02fb4e` | Step 2.1 — rollover-safe clock |
| `aa9a155` | docs — measured arm geometry / arc compensation note |
| `9af03a6` | Step 2.2 — fixed-capacity event queue |
| `72c9e9d` | Step 2.3 (layers 1–2) — host link, `PING`/`PONG` |
| `9184943` | fix — missing `Arduino.h` include |
| `0d05d99` | Step 2.3 (layer 3) — host link hardening |
| `23e492a` | docs — these progress notes |
| `094e310` | Step 2.4 — Python serial link and clock sync (`v0.2.0`) |
| `a6f22f0` | Step 3.1 (started, branch `wip/servo-characterisation`) |
| `a93e843` | Step 3.1 closed: `wip/servo-characterisation` merged into `main` |
| `9391816` | Step 3.2 (started, branch `wip/arm-geometry`): `arm_jog` experiment |
| `8b57315` | Step 3.2: arm site-angle figures |

**How Step 2.3 was closed out (2026-10-20).**

- *A lesson from the first hardware check.* The first serial-monitor run
  still showed the old behaviour (bare `PING` and blank lines both gave
  `ERR unknown command`). The source was right; the board was running the
  previous build. `pio run -e teensy41` only compiles — flashing needs
  `pio run -e teensy41 -t upload`, with the serial monitor closed so it
  isn't holding the port. When the board disagrees with the source, check
  what was actually uploaded before debugging the code.
- *Acceptance results* after uploading the right build:

  | Sent | Reply |
  |---|---|
  | `PING` | `ERR PING needs a sequence number, e.g. PING 1` |
  | `PING 1` | `PONG 1 t_us=19571818` |
  | `PING7`, `PING2` | `ERR unknown command: ...` (the full-string `strcmp` is doing its job) |
  | `jhgjkg` | `ERR unknown command: jhgjkg` |
  | over-long line | `ERR line too long, max 127 chars` |
  | blank line | no reply |

- *A comment fix.* `HostLink.h` used to say "index 127 is always `'\0'`".
  That was wrong: the terminator is written at `buffer_[length_]`
  (index 6 for `PING 7`). Index 127 is only *reserved*, so the terminator
  always has room. The comment now says that.
- It's a `fix(firmware):` commit, separate from the original `feat` in
  `72c9e9d`, because it changes existing behaviour rather than adding
  something new. These notes went in a `docs:` commit of their own: one
  step, one commit.

**Learning walkthrough of HostLink, in progress.** Parts 1 (the header:
`#pragma once`, `public`/`private`, `static constexpr`, why the buffer
lives inside the object) and 2 (`poll()` as a state machine that keeps its
place between calls) are done. Two questions answered along the way:

- *Why is `handle_line()` private?* `poll()` is the only code that
  guarantees the line ends in `'\0'`. C string functions (`strcmp`,
  `Serial.print`) have no length; they read until they hit a zero byte. A
  caller that skipped `poll()` could pass bytes with no terminator, and
  `Serial.println(line)` would print memory until it happened to find
  a zero.
- *Why is `length_` a member and not a local variable?* A line can arrive
  split across two `poll()` calls (`PI`, then `NG 1\n`). A local would go
  back to 0 on every call, so the `PI` would be lost and the reply would be
  `ERR unknown command: NG 1`.

Still to do: part 3 (`handle_line()` — C strings, `strncmp` against
`strcmp`, pointer arithmetic) and part 4 (how `main.cpp` and `protocol.h`
connect).

**Step 2.4 (done, 2026-10-20, `094e310`, tag `v0.2.0`).** The Python side
of the host link: `rhi/protocol.py` copies `protocol.h`, and `rhi/link.py`
syncs the two clocks using the minimum-round-trip ping. Measured: about
+/- 280 us uncertainty, three runs agree, and drift is below about 10 ppm.
The numbers are in `docs/timing-notes.md`. Group 2 is complete.

**Step 3.1 (done 2026-11-22, merged to `main` as `a93e843`).** Measuring
the servos before building anything on them. Done, all recorded in
`docs/servo-characterisation.md`:

- Test program `firmware/experiments/servo_char` (a separate PlatformIO
  project; commands `HZ`, `US`, `OFF`, `SHOW`; sends no pulses at start-up).
- Safety check of the voltage on the signal wire: PDI 50 mV, EMAX 0 V, so
  a direct connection through 1 kohm is safe until the 74AHCT125 arrives.
- Power-up noise: harmless (no movement). Rule: start the pulses first, then
  switch on the servo power.
- PDI pulse range: moves through at least 500-2500 us, quiet at both ends.
- PDI angle (2026-11-22): linear, about 0.093 deg per us, about 186 deg over
  500-2500 us. The 2500 us end is estimated (past the protractor scale).
- A servo with its signal wire left unconnected can twitch on its own (the
  open wire picks up noise). Unused signal wires are tied to GND while one
  servo is tested; a 10 kohm pulldown per 74AHCT125 input is planned for the
  final rig (not yet written into PROJECT.md section 2.7).

- PDI refresh rate (2026-11-22): 333 Hz passes (no buzz, no heat, no drift,
  clean 3.00 ms / 1.52 ms signal on the logic analyser). Decision: 333 Hz.
  A 5 deg difference at 2000 us between 50 Hz and 333 Hz was left unresolved:
  the hand protractor is only good to 1-2 deg.
- EMAX pulse range (2026-11-22): the horn moves between about 800 and 2100 us,
  but is unreliable near both ends (small commands sometimes ignored; after
  being sent to an end it did not answer a step 50-100 us back). Operating
  range set to 900-1900 us so the plastic gears are never driven into a stop.
- EMAX angle: a straight line, about 0.095 deg per us (10.5 us per deg), about
  106 deg between 900 and 2000 us. Revisits to the same pulse agree to 0-5 deg
  (mean 2.4 deg), which is about the protractor's 5 deg reading step, so the
  numbers are provisional. The first session, with the protractor badly placed,
  disagreed by 5-7 deg. Centre pulse about 1450 us (midpoint of the ends, not
  confirmed).
- EMAX shaking: slight, at 1200 us only, at 50 Hz, untouched. Avoid placing a
  site there.
- EMAX refresh rate: 100 Hz moves smoothly; 200 Hz and 333 Hz buzz when moving.
  Decision: 100 Hz. The EMAX pins therefore need a different FlexPWM submodule
  from the PDI pins, which run at 333 Hz (PROJECT.md section 2.8).

- Step 3.1 closed out (2026-11-22): EMAX idle current about 10 mA. Step
  response, overshoot and dead band (both servos), the PDI idle current and a
  2 minute hold at 100 Hz for the EMAX were deliberately not measured on the
  bare servos. A bare horn does not carry the arm's inertia, 1 us is finer than
  the hand protractor reads, and the timing figures that matter are measured on
  the arm. Reasons are recorded in `docs/servo-characterisation.md`.

**Step 3.2 (started 2026-11-23, branch `wip/arm-geometry`).** Finding, by hand,
three settings for each of the five finger sites on each arm: RAISED (proximal,
one value), AIM (distal, one per site) and TOUCH (proximal, one per site). See
`docs/figures/arm-site-angles.svg`.
- Pin plan decided: top arm PDI on pin 3 and EMAX on pin 6; down arm PDI on pin 2
  and EMAX on pin 9. Pins 2 and 3 share one PWM timer (PDIs at 333 Hz) and pins
  6 and 9 another (EMAXes at 100 Hz), read from the pin table in the Teensy core.
  Not yet verified on the logic analyser. Recorded in `docs/geometry.md`.
- Test program `firmware/experiments/arm_jog` (a separate PlatformIO project):
  typed absolute and relative moves per joint, slew-limited, with per-joint
  pulse limits, and a `MARK` command that prints all four pulses on one line.
  It builds without warnings; it has not been uploaded or run on the arm yet.
- Figures: `docs/figures/arm-site-angles.svg` and `docs/figures/arm-tip-arc.svg`.
  The second predicts the extra proximal rotation each site needs, about +4,
  +1, 0, +1 and +2.5 degrees for sites 1 to 5 (flat finger, arm near
  horizontal, site 1 nearest the base). The bench work confirms or corrects it.
- `docs/geometry.md` is now a template with a table per arm, pulses first.

**Plan for 2026-11-24** (done in part: ZERO poses, four-site AIM and TOUCH, the
arm_taps experiment; superseded on 2026-11-25, see below):
1. Ask which pulse each servo was holding when its horn and arm were attached:
   that is each joint's first command, so the arm does not move at power-on.
2. With the servo power OFF, upload `arm_jog` and check the four signals on the
   logic analyser (pins 2 and 3: 3.00 ms period; pins 6 and 9: 10.0 ms).
3. First power-up with the hand out of the way. Then find RAISED, then AIM and
   TOUCH for each site, paste the `MARK` lines into `docs/geometry.md` and
   photograph each site.
Acceptance (PROJECT.md Step 3.2): five sites reachable, the tip lands cleanly
on each, and the raised position clears the finger everywhere. Commit message:
`docs: record arm geometry and per-site joint angles`.

Not done yet: the pin plan is not recorded in PROJECT.md section 2.8, and the
hand's orientation along the arm (which end of the finger faces the base) is
assumed in the figures.

**Step 3.2, 2026-11-25: sites taught by contact.** The four fixed sites of
deviation D6 did not follow each participant's finger, so D6 is withdrawn and
the five anatomical sites of Chancel et al. are back. They are marked on each
hand and taught by contact: per site, AIM (distal) and CONTACT (proximal,
first skin contact). RAISED = CONTACT + S and TOUCH = CONTACT - P, with S and P
the same for every participant (bench start S = 100 us, P = 10 us), so every
tap is the same stroke. The MCP height and the finger's slope are captured by
the teaching itself, with no model.
- Procedure: `docs/site-calibration.md`. PROJECT.md 1.4, 1.12 (U1, U4, U5),
  1.13 (D6), 2.2, Steps 3.2, 4.1 and 6.10 updated to match.
- `arm_jog`: key mode `c` + site records AIM and CONTACT, `p` prints a block
  for `firmware/experiments/common/calibration.h`.
- `arm_taps`: five sites read from `calibration.h`; RAISED and TOUCH per site;
  `PRESS` sets P and `START` sets S; a run is refused while a table has a 0.

**Resume here:** teach the rubber hand (top arm) and the author's own hand as
a test participant (down arm), following `docs/site-calibration.md` sections
4 to 7. Paste both `p` blocks into `calibration.h`, upload `arm_taps`, run the
ink test, and record the tables and errors in `site-calibration.md` section 9.
