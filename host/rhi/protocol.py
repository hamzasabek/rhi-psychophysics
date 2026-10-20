"""The serial message contract, host side. Mirrors firmware/src/protocol.h.

`protocol.h` is the source of truth for what crosses the USB cable. This module
is its Python twin: it knows how to WRITE every command the host sends and how
to READ every reply the Teensy sends. It holds no state and touches no serial
port - it only turns text into data and data into text. That is what makes it
testable with plain string literals and no hardware (see tests/test_protocol.py).

Current grammar (one message per line, plain ASCII, ended by '\n'):

    Host  -> Teensy:   PING <seq>
    Teensy -> Host:    PONG <seq> t_us=<n>
    Teensy -> Host:    ERR <msg>

If protocol.h changes, this file and its tests must change in the same commit.
"""

import re  # regular expressions: describe the exact shape a line must have
from dataclasses import dataclass  # concise classes for fixed-shape data

# ---------------------------------------------------------------------------
# Constants shared with the firmware.
# ---------------------------------------------------------------------------

# The Teensy's clock is a 32-bit unsigned counter of microseconds, so t_us
# values live in 0 .. 2**32 - 1 and then wrap back to 0.
#   2**32 us = 4 294 967 296 us = 4294.97 s = 71.6 minutes between wraps.
TEENSY_CLOCK_WRAP_US = 2**32

# HostLink's line buffer holds 128 bytes, one of them reserved for the '\0'
# terminator, so the longest line the firmware accepts is 127 characters.
MAX_LINE_CHARS = 127

# ---------------------------------------------------------------------------
# The shape of a PONG line, written as a regular expression.
#
#   PONG       the literal verb
#   (\d+)      group 1: one or more digits - the echoed sequence number
#    t_us=     a single space, then the literal key
#   (\d+)      group 2: one or more digits - the Teensy clock reading
#
# re.fullmatch (used below) requires the WHOLE line to fit this pattern, so
# "PONG 1 t_us=5 extra" or "PONG 1  t_us=5" (two spaces) are rejected rather
# than half-understood. re.ASCII makes \d mean only 0-9; without it, Python
# would also accept other Unicode digits such as '٣' (Arabic-Indic three).
# ---------------------------------------------------------------------------
_PONG_PATTERN = re.compile(r"PONG (\d+) t_us=(\d+)", re.ASCII)


class ProtocolError(ValueError):
    """A line arrived that does not follow the contract in protocol.h.

    Subclassing ValueError means code that already catches ValueError
    ("this value is malformed") also catches this, while code that wants to
    be specific can catch ProtocolError alone.
    """


@dataclass(frozen=True)
class Pong:
    """The Teensy's reply to PING.

    frozen=True makes instances read-only after creation: a received message
    is a record of something that happened, and nobody should edit it later.
    """

    seq: int  # the sequence number the host sent, echoed back unchanged
    t_us: int  # Teensy clock (micros()) at the moment it built the reply


@dataclass(frozen=True)
class Err:
    """The Teensy reporting that something went wrong. It keeps running."""

    msg: str  # human-readable description, e.g. "unknown command: PING7"


# A received message is exactly one of these. `Pong | Err` reads as
# "a Pong or an Err"; the type checker then insists callers handle both.
Message = Pong | Err


def format_ping(seq: int) -> str:
    """Build the PING command line for sequence number `seq`.

    The sequence number lets the host match a reply to its request: if a
    stale PONG from an earlier, timed-out ping is still in the pipe, its seq
    will not match and the host can skip it.

    Worked example:
        format_ping(7)  ->  "PING 7\\n"   (6 visible characters + newline)

    Negative numbers are refused because the firmware's reply is parsed with
    \\d+ (digits only), so a "-1" would come back as an unparseable PONG.
    """
    if seq < 0:  # refuse what the reply parser could not read back
        raise ValueError(f"seq must be >= 0, got {seq}")
    return f"PING {seq}\n"  # verb, one space, number, newline - as protocol.h


def parse_line(line: str) -> Message:
    """Turn one line received from the Teensy into a Pong or an Err.

    Trailing '\\r' and '\\n' are stripped first, so it does not matter whether
    the caller already removed the line ending (Teensy's Serial.println sends
    "\\r\\n").

    Worked example:
        parse_line("PONG 1 t_us=64011034\\r\\n")
          strip line ending  -> "PONG 1 t_us=64011034"
          fullmatch groups   -> ("1", "64011034")
          result             -> Pong(seq=1, t_us=64011034)

        parse_line("ERR unknown command: PING7")
          starts with "ERR " -> Err(msg="unknown command: PING7")

    Anything else raises ProtocolError rather than guessing. Silently
    misreading a line is how a timing bug hides for three months.
    """
    text = line.rstrip("\r\n")  # drop the line ending, whatever form it took

    match = _PONG_PATTERN.fullmatch(text)  # None unless the WHOLE line fits
    if match is not None:  # it is a well-formed PONG
        seq = int(match.group(1))  # "1"        -> 1
        t_us = int(match.group(2))  # "64011034" -> 64011034
        if t_us >= TEENSY_CLOCK_WRAP_US:  # a uint32 can never be this big
            raise ProtocolError(f"t_us out of 32-bit range: {text!r}")
        return Pong(seq=seq, t_us=t_us)

    if text.startswith("ERR "):  # "ERR " + message, exactly as HostLink prints
        return Err(msg=text[4:])  # everything after the 4-character "ERR "

    # Neither shape fits. !r shows the text with quotes and escapes, so an
    # empty line or stray whitespace is visible in the error message.
    raise ProtocolError(f"unrecognised line from Teensy: {text!r}")
