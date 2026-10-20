"""Tests for rhi.link - clock-sync arithmetic and the Link class.

No Teensy needed. The arithmetic is tested with hand-made numbers (the same
worked examples as in the docstrings). The Link class is tested against
FakeTeensy, a small stand-in that answers PING the way HostLink does, paired
with a fake host clock whose readings we script in advance.

Run with:  pytest host/tests
"""

import pytest  # test runner; also provides pytest.raises and approx

from rhi.link import (
    ClockOffset,
    Link,
    SyncSample,
    drift_ppm,
    estimate_offset,
    offsets_agree,
    wrap_diff_us,
)
from rhi.protocol import ProtocolError

# ---------------------------------------------------------------------------
# Test doubles
# ---------------------------------------------------------------------------


class FakeTeensy:
    """Pretends to be the serial port with the firmware behind it.

    write() reads a "PING <seq>" and queues the PONG reply; readline() hands
    queued lines back one at a time, and b"" (a timeout) when none are left.
    `extra_before` lets a test plant lines that arrive before the real reply,
    e.g. a stale PONG.
    """

    def __init__(self, teensy_us: list[int], extra_before: list[bytes] | None = None):
        self._teensy_us = list(teensy_us)  # t_us to report, one per PING
        self._queue: list[bytes] = list(extra_before or [])  # lines to send
        self.closed = False  # so a test can check close() was called

    def write(self, data: bytes) -> int:
        text = data.decode("ascii").rstrip("\n")  # b"PING 3\n" -> "PING 3"
        seq = text[5:]  # echo verbatim, like handle_line()'s `line + 5`
        t_us = self._teensy_us.pop(0)  # the next scripted clock reading
        self._queue.append(f"PONG {seq} t_us={t_us}\r\n".encode("ascii"))
        return len(data)

    def flush(self) -> None:
        pass  # nothing is buffered in a fake

    def readline(self) -> bytes:
        return self._queue.pop(0) if self._queue else b""  # b"" means timeout

    def reset_input_buffer(self) -> None:
        self._queue.clear()

    def close(self) -> None:
        self.closed = True


def scripted_clock(readings_ns: list[int]):
    """A fake host clock: each call returns the next value from the list."""
    it = iter(readings_ns)  # an iterator remembers where it is between calls
    return lambda: next(it)


# ---------------------------------------------------------------------------
# wrap_diff_us: Clock.h's rollover trick in Python
# ---------------------------------------------------------------------------


def test_wrap_diff_ordinary() -> None:
    assert wrap_diff_us(1_000, 400) == 600  # no wrap involved
    assert wrap_diff_us(400, 1_000) == -600  # "later" is actually earlier


def test_wrap_diff_across_rollover() -> None:
    """The same numbers as the Clock.h example: 16 us across the wrap."""
    assert wrap_diff_us(10, 4_294_967_290) == 16
    assert wrap_diff_us(4_294_967_290, 10) == -16  # and the other direction


# ---------------------------------------------------------------------------
# estimate_offset
# ---------------------------------------------------------------------------

# The two samples from the estimate_offset() docstring. Host times in ns.
SAMPLE_A = SyncSample(1_000_000_000, 1_000_400_000, 64_011_034)  # RTT 400 us
SAMPLE_B = SyncSample(2_000_000_000, 2_003_000_000, 65_012_534)  # RTT 3000 us


def test_sample_properties() -> None:
    assert SAMPLE_A.rtt_us == 400.0
    assert SAMPLE_A.host_mid_us == 1_000_200.0


def test_estimate_offset_picks_minimum_round_trip() -> None:
    """Order must not matter: B first, A second, A still wins."""
    result = estimate_offset([SAMPLE_B, SAMPLE_A])
    assert result.offset_us == 63_010_834.0  # 64 011 034 - 1 000 200
    assert result.uncertainty_us == 200.0  # 400 / 2
    assert result.min_rtt_us == 400.0
    assert result.n == 2
    assert result.host_us == 1_000_200.0


def test_estimate_offset_needs_samples() -> None:
    with pytest.raises(ValueError):
        estimate_offset([])


# ---------------------------------------------------------------------------
# offsets_agree and drift_ppm
# ---------------------------------------------------------------------------


def offset(value_us: float, unc_us: float, host_us: float = 0.0) -> ClockOffset:
    """Shorthand for building a ClockOffset in these tests."""
    return ClockOffset(value_us, unc_us, 2 * unc_us, 20, host_us)


def test_offsets_agree_when_intervals_overlap() -> None:
    """The docstring example: [-200,200], [-120,180], [-300,200] overlap."""
    assert offsets_agree([offset(0, 200), offset(30, 150), offset(-50, 250)])


def test_offsets_disagree_when_intervals_do_not_overlap() -> None:
    assert not offsets_agree([offset(0, 100), offset(500, 100)])  # gap 200..400


def test_offsets_agree_across_rollover() -> None:
    """Offset 10 and offset 2**32 - 10 are 20 us apart, not 4.29 billion."""
    assert offsets_agree([offset(10, 50), offset(4_294_967_286, 50)])


def test_drift_ppm_docstring_example() -> None:
    a = offset(0, 200, host_us=0)
    b = offset(1_200, 200, host_us=60_000_000)  # 60 s later, offset +1200 us
    ppm, ppm_unc = drift_ppm(a, b)
    assert ppm == pytest.approx(20.0)
    assert ppm_unc == pytest.approx(6.667, abs=0.001)


def test_drift_ppm_across_rollover() -> None:
    """The offset crossing the wrap must not look like a huge jump."""
    a = offset(4_294_967_000, 100, host_us=0)
    b = offset(304, 100, host_us=60_000_000)  # +600 us, but across the wrap
    ppm, _ = drift_ppm(a, b)
    assert ppm == pytest.approx(10.0)  # 600 / 60 000 000 * 1e6


def test_drift_ppm_refuses_wrong_order() -> None:
    with pytest.raises(ValueError):
        drift_ppm(offset(0, 1, host_us=5), offset(0, 1, host_us=5))


# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------


def test_ping_timestamps_both_ends() -> None:
    port = FakeTeensy(teensy_us=[64_011_034])
    link = Link(port, clock_ns=scripted_clock([1_000_000_000, 1_000_400_000]))
    assert link.ping() == SAMPLE_A


def test_sync_clock_picks_fastest_of_n() -> None:
    """Three pings with round trips of 900, 300 and 600 us: the 300 wins."""
    clock = scripted_clock(
        [
            0, 900_000,  # ping 1: RTT 900 us
            2_000_000, 2_300_000,  # ping 2: RTT 300 us, midpoint 2150 us
            4_000_000, 4_600_000,  # ping 3: RTT 600 us
        ]
    )  # fmt: skip
    port = FakeTeensy(teensy_us=[10_450, 12_150, 14_300])
    result = Link(port, clock_ns=clock).sync_clock(n=3)
    assert result.min_rtt_us == 300.0
    assert result.offset_us == 10_000.0  # 12 150 - 2 150
    assert result.uncertainty_us == 150.0


def test_ping_skips_stale_pong() -> None:
    """A PONG left over from an earlier, timed-out ping is ignored."""
    stale = [b"PONG 999 t_us=1\r\n"]  # wrong seq: Link's first ping is seq 1
    port = FakeTeensy(teensy_us=[64_011_034], extra_before=stale)
    # Three clock reads: send, after the stale line, after the real reply.
    link = Link(port, clock_ns=scripted_clock([1_000_000_000, 0, 1_000_400_000]))
    assert link.ping() == SAMPLE_A


def test_ping_raises_on_err_reply() -> None:
    port = FakeTeensy(teensy_us=[0], extra_before=[b"ERR unknown command: X\r\n"])
    link = Link(port, clock_ns=scripted_clock([0, 1]))
    with pytest.raises(ProtocolError, match="unknown command"):
        link.ping()


def test_read_line_times_out() -> None:
    """readline() returning b"" (pyserial's timeout) becomes TimeoutError."""
    link = Link(FakeTeensy(teensy_us=[]))
    with pytest.raises(TimeoutError):
        link.read_line()


def test_context_manager_closes_port() -> None:
    port = FakeTeensy(teensy_us=[])
    with Link(port):
        pass
    assert port.closed
