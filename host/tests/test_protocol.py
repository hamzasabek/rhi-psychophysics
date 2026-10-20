"""Tests for rhi.protocol - the Python mirror of firmware/src/protocol.h.

Every reply string below is copied character for character from what the
firmware prints (HostLink.cpp) or from real serial-monitor output. If someone
changes the wording on one side of the cable and not the other, these tests
are what notice.

Run with:  pytest host/tests
"""

import pytest  # test runner; also provides pytest.raises and parametrize

from rhi.protocol import (
    MAX_LINE_CHARS,
    TEENSY_CLOCK_WRAP_US,
    Err,
    Pong,
    ProtocolError,
    format_ping,
    parse_line,
)

# ---------------------------------------------------------------------------
# Host -> Teensy
# ---------------------------------------------------------------------------


def test_format_ping_matches_contract() -> None:
    """protocol.h: `PING <seq>`, one line ended by '\\n'."""
    assert format_ping(1) == "PING 1\n"
    assert format_ping(4096) == "PING 4096\n"


def test_format_ping_refuses_negative_seq() -> None:
    """A negative seq would come back as a PONG the parser cannot read."""
    with pytest.raises(ValueError):
        format_ping(-1)


def test_format_ping_fits_firmware_buffer() -> None:
    """Even a huge seq stays far below HostLink's 127-character line limit."""
    assert len(format_ping(10**18).rstrip("\n")) <= MAX_LINE_CHARS


# ---------------------------------------------------------------------------
# Teensy -> Host: PONG
# ---------------------------------------------------------------------------


def test_parse_pong_from_real_hardware() -> None:
    """Copied from the Step 2.3 serial-monitor session."""
    assert parse_line("PONG 1 t_us=64011034") == Pong(seq=1, t_us=64011034)


@pytest.mark.parametrize("ending", ["", "\n", "\r\n"])
def test_parse_pong_ignores_line_ending(ending: str) -> None:
    """Serial.println() sends "\\r\\n"; callers may or may not have stripped it."""
    assert parse_line("PONG 7 t_us=3141592" + ending) == Pong(seq=7, t_us=3141592)


def test_parse_pong_accepts_largest_clock_value() -> None:
    """2**32 - 1 is the last micros() value before the wrap to 0."""
    top = TEENSY_CLOCK_WRAP_US - 1  # 4 294 967 295
    assert parse_line(f"PONG 2 t_us={top}") == Pong(seq=2, t_us=top)


@pytest.mark.parametrize(
    "line",
    [
        "PONG 1",  # timestamp missing
        "PONG t_us=5",  # seq missing
        "PONG x t_us=5",  # seq not a number
        "PONG 1 t_us=",  # timestamp empty
        "PONG 1 t_us=-5",  # a uint32 is never negative
        "PONG 1  t_us=5",  # two spaces
        "PONG 1 t_us=5 extra",  # trailing junk
        "PONG 1 t_us=4294967296",  # 2**32: one past what a uint32 can hold
        "pong 1 t_us=5",  # the protocol is case-sensitive
    ],
)
def test_parse_rejects_malformed_pong(line: str) -> None:
    """Half-right is wrong: refuse anything that is not exactly the contract."""
    with pytest.raises(ProtocolError):
        parse_line(line)


# ---------------------------------------------------------------------------
# Teensy -> Host: ERR
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("line", "msg"),
    [
        # The three ERR messages HostLink.cpp can produce today.
        ("ERR line too long, max 127 chars", "line too long, max 127 chars"),
        (
            "ERR PING needs a sequence number, e.g. PING 1",
            "PING needs a sequence number, e.g. PING 1",
        ),
        ("ERR unknown command: PING7", "unknown command: PING7"),
    ],
)
def test_parse_firmware_err_messages(line: str, msg: str) -> None:
    """Every ERR the firmware sends parses, and the message survives intact."""
    assert parse_line(line + "\r\n") == Err(msg=msg)


# ---------------------------------------------------------------------------
# Anything else
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("line", ["", "HELLO", "ERR", "ERRx", "PING 1"])
def test_parse_rejects_unknown_lines(line: str) -> None:
    """Includes "PING 1": that is a host command, never a Teensy reply."""
    with pytest.raises(ProtocolError):
        parse_line(line)


def test_round_trip_through_simulated_firmware() -> None:
    """format_ping -> what HostLink does -> parse_line gives the seq back.

    HostLink echoes everything after "PING " verbatim (line + 5 in
    handle_line()). Doing the same here proves the two halves of the
    contract fit together.
    """
    command = format_ping(42).rstrip("\n")  # what the firmware sees: "PING 42"
    reply = f"PONG {command[5:]} t_us=1000\r\n"  # HostLink's echo, by hand
    assert parse_line(reply) == Pong(seq=42, t_us=1000)
