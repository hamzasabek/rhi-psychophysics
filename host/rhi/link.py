"""Serial link to the Teensy, and host-Teensy clock synchronisation.

The two machines have separate clocks. The Teensy counts microseconds since it
powered on; the host counts nanoseconds from some point of its own. Every
Teensy timestamp the host receives later (tap times, contact times) is only
useful once we know the OFFSET between the two clocks:

    teensy_us = host_us + offset_us

How we estimate it - one PING round trip:

    host:    h1 ---- PING 7 ---->                <---- PONG 7 t_us=T ---- h2
    Teensy:                  stamps T somewhere in here

The Teensy read its clock at some instant between h1 (host sent) and h2 (host
received). The best guess is the midpoint, and the worst-case error is half
the round trip:

    offset      = T - (h1 + h2) / 2
    uncertainty = (h2 - h1) / 2

We repeat this n times and keep the sample with the SMALLEST round trip. Round
trips have a hard physical floor plus a long tail of slow ones, caused by the
operating system pausing us. The minimum is the sample least contaminated by
that tail; an average would be dragged upward by it.

Layering: this module talks to the serial port and uses rhi.protocol to read
and write the text. The pure arithmetic (estimate_offset, offsets_agree,
drift_ppm) touches no port and no clock, so tests can feed it hand-made
numbers.

Run from the command line:
    python -m rhi.link --port COM5 --sync
    python -m rhi.link --port COM5 --sync --repeat 3
    python -m rhi.link --port COM5 --sync --repeat 2 --interval 60
"""

import argparse  # command-line option parsing for main()
import time  # perf_counter_ns (host clock) and sleep (host side only)
from collections.abc import Callable, Sequence  # types for function arguments
from dataclasses import dataclass  # concise classes for fixed-shape data
from typing import Protocol  # describe "anything with these methods"

import serial  # pyserial: the real USB serial port

from rhi.protocol import (
    TEENSY_CLOCK_WRAP_US,
    Err,
    Pong,
    ProtocolError,
    format_ping,
    parse_line,
)

# Half the wrap: the largest gap the signed-difference trick can represent.
# 2**31 us = 2147.5 s = 35.8 minutes. Same limit as clk::is_due() in Clock.h.
_HALF_WRAP_US = TEENSY_CLOCK_WRAP_US // 2


# ---------------------------------------------------------------------------
# Data records
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class SyncSample:
    """One PING/PONG round trip: two host clock readings around one Teensy one.

    Host times are kept in integer nanoseconds exactly as perf_counter_ns()
    returned them, so no precision is lost before the arithmetic below.
    """

    host_send_ns: int  # host clock just before the PING was written (h1)
    host_recv_ns: int  # host clock just after the PONG line arrived (h2)
    teensy_us: int  # t_us from the PONG (T)

    @property
    def rtt_us(self) -> float:
        """Round-trip time in microseconds.

        Worked example: send 1 000 000 000 ns, recv 1 000 400 000 ns
            (1 000 400 000 - 1 000 000 000) / 1000 = 400.0 us
        """
        return (self.host_recv_ns - self.host_send_ns) / 1000  # ns -> us

    @property
    def host_mid_us(self) -> float:
        """Midpoint of the round trip in microseconds: our best guess for the
        host time at which the Teensy read its clock.

        Worked example: send 1 000 000 000 ns, recv 1 000 400 000 ns
            (1 000 000 000 + 1 000 400 000) / 2 / 1000 = 1 000 200.0 us
        """
        return (self.host_send_ns + self.host_recv_ns) / 2000  # mean, ns -> us


@dataclass(frozen=True)
class ClockOffset:
    """The result of one synchronisation: teensy_us = host_us + offset_us.

    offset_us is only meaningful modulo 2**32, because the Teensy clock wraps.
    Compare two offsets with wrap_diff_us(), never with a bare subtraction.
    """

    offset_us: float  # Teensy clock minus host clock, in microseconds
    uncertainty_us: float  # +/- bound: half the round trip of the best sample
    min_rtt_us: float  # round trip of the best sample
    n: int  # how many samples it was chosen from
    host_us: float  # host time (us) at which this offset was measured


# ---------------------------------------------------------------------------
# Pure arithmetic - no port, no clock, fully testable
# ---------------------------------------------------------------------------


def wrap_diff_us(later: float, earlier: float) -> float:
    """`later - earlier` on the Teensy's 32-bit clock, correct across a wrap.

    This is clk::is_due()'s trick from Clock.h, in Python. Python integers
    never overflow, so we wrap by hand: shift by half the range, take the
    remainder modulo 2**32 (Python's % never returns a negative), shift back.
    The result is always in -2**31 .. 2**31 - 1.

    Worked example - the same numbers as Clock.h:
        later = 10, earlier = 4 294 967 290   (16 us apart, across the wrap)
        10 - 4 294 967 290                    = -4 294 967 280
        + 2 147 483 648 (half wrap)           = -2 147 483 632
        % 4 294 967 296                       =  2 147 483 664
        - 2 147 483 648                       =             16   <- correct

    Valid while the true gap is under 2**31 us (35.8 minutes).
    """
    return (later - earlier + _HALF_WRAP_US) % TEENSY_CLOCK_WRAP_US - _HALF_WRAP_US


def estimate_offset(samples: Sequence[SyncSample]) -> ClockOffset:
    """Pick the minimum-round-trip sample and turn it into a ClockOffset.

    Worked example with two samples (times in us):

        sample   h1          T            h2          RTT     midpoint
        A        1 000 000   64 011 034   1 000 400     400   1 000 200
        B        2 000 000   65 012 534   2 003 000   3 000   2 001 500

        B's round trip is 7.5x longer: the OS paused us. Pick A.
        offset      = 64 011 034 - 1 000 200 = 63 010 834 us
        uncertainty = 400 / 2                =        200 us

    Only the chosen sample's own T is used, so a wrap of the Teensy clock
    between samples cannot corrupt the result.
    """
    if not samples:  # min() of nothing is undefined - say so clearly
        raise ValueError("need at least one sample")
    best = min(samples, key=lambda s: s.rtt_us)  # the least-delayed round trip
    return ClockOffset(
        offset_us=best.teensy_us - best.host_mid_us,  # T - midpoint
        uncertainty_us=best.rtt_us / 2,  # T lies within +/- half the RTT
        min_rtt_us=best.rtt_us,
        n=len(samples),
        host_us=best.host_mid_us,  # when this offset was true
    )


def offsets_agree(offsets: Sequence[ClockOffset]) -> bool:
    """True if every offset's +/- interval overlaps a common value.

    Each offset says "the truth lies in [offset - u, offset + u]". If all
    those intervals share at least one point, the measurements agree. That
    is the Step 2.4 acceptance check.

    Offsets are first expressed relative to the first one with
    wrap_diff_us(), so a wrap between measurements does not make them look
    4 294 967 296 us apart.

    Worked example (relative offsets, us):
        0 +/- 200   ->  [-200, 200]
        30 +/- 150  ->  [-120, 180]
        -50 +/- 250 ->  [-300, 200]
        highest lower edge = -120, lowest upper edge = 180
        -120 <= 180, so they overlap: agree.
    """
    if not offsets:  # nothing to compare
        raise ValueError("need at least one offset")
    first = offsets[0].offset_us  # reference point for the relative offsets
    uppers = []  # top edge of each interval, relative to the first offset
    lowers = []  # bottom edge of each interval, relative to the first offset
    for o in offsets:
        rel = wrap_diff_us(o.offset_us, first)  # wrap-safe relative offset
        uppers.append(rel + o.uncertainty_us)
        lowers.append(rel - o.uncertainty_us)
    return max(lowers) <= min(uppers)  # the intervals share a point


def drift_ppm(a: ClockOffset, b: ClockOffset) -> tuple[float, float]:
    """How fast the offset changes, in parts per million, with its +/- bound.

    The two crystals never tick at exactly the same rate, so the offset
    slowly changes. Positive drift: the Teensy clock runs fast relative to
    the host.

        drift       = change in offset / host time elapsed * 1e6
        uncertainty = (u_a + u_b)      / host time elapsed * 1e6

    Worked example: offsets 60 s apart, offset grew by 1 200 us, each +/- 200:
        drift       = 1 200 / 60 000 000 * 1e6 = 20.0 ppm
        uncertainty =   400 / 60 000 000 * 1e6 =  6.7 ppm

    The uncertainty shrinks as the gap grows: 1 s apart, the same +/- 200 us
    gives +/- 400 ppm - useless. Measure drift over a minute or more.
    """
    elapsed_us = b.host_us - a.host_us  # host time between the measurements
    if elapsed_us <= 0:  # b must come after a
        raise ValueError("b must be measured after a")
    change_us = wrap_diff_us(b.offset_us, a.offset_us)  # wrap-safe difference
    ppm = change_us / elapsed_us * 1e6  # fraction -> parts per million
    ppm_unc = (a.uncertainty_us + b.uncertainty_us) / elapsed_us * 1e6
    return ppm, ppm_unc


# ---------------------------------------------------------------------------
# The link itself
# ---------------------------------------------------------------------------


class SerialLike(Protocol):
    """The handful of pyserial methods Link uses.

    A typing.Protocol says "any object with these methods will do". The real
    serial.Serial qualifies; so does the small fake in tests/test_link.py.
    Link never needs to know which one it was given.
    """

    def write(self, data: bytes) -> int | None: ...  # send raw bytes
    def flush(self) -> None: ...  # wait until written bytes have left
    def readline(self) -> bytes: ...  # read up to '\n', or b"" on timeout
    def reset_input_buffer(self) -> None: ...  # throw away unread bytes
    def close(self) -> None: ...  # release the port


class Link:
    """A line-based conversation with the Teensy over a serial port.

    Use it as a context manager so the port is always released, even if an
    exception escapes:

        with open("COM5") as link:
            offset = link.sync_clock()
    """

    def __init__(
        self,
        port: SerialLike,
        clock_ns: Callable[[], int] = time.perf_counter_ns,
    ) -> None:
        """Wrap an already-open port. Normally you call open() instead.

        clock_ns is the host clock. perf_counter_ns is the right one: on
        Windows it reads the hardware performance counter (sub-microsecond
        resolution) and it never jumps backwards. time.time() would be wrong:
        it follows the wall clock, which Windows adjusts when it syncs to an
        internet time server. Tests pass a fake clock here.
        """
        self._port = port  # where bytes go in and out
        self._clock_ns = clock_ns  # the host clock used for every timestamp
        self._next_seq = 1  # sequence number for the next PING

    def __enter__(self) -> "Link":
        return self  # `with open(...) as link:` binds link to this object

    def __exit__(self, *exc_info: object) -> None:
        self.close()  # runs however the with-block ends, exception or not

    def close(self) -> None:
        """Release the serial port so other programs (e.g. a monitor) can use it."""
        self._port.close()

    def send(self, line: str) -> None:
        """Send one line to the Teensy, adding the '\\n' if it is missing.

        ASCII encoding: the protocol is plain ASCII, and .encode("ascii")
        raises immediately if a non-ASCII character slipped in, instead of
        sending bytes the firmware would misread.
        """
        if not line.endswith("\n"):  # the firmware acts only on complete lines
            line += "\n"
        self._port.write(line.encode("ascii"))  # str -> bytes
        self._port.flush()  # push it out now, not when a buffer fills

    def read_line(self) -> str:
        """Read one line from the Teensy, without its line ending.

        pyserial's readline() returns early with whatever it has (possibly
        b"") when the port timeout expires. A line without '\\n' therefore
        means "timed out", and we say so rather than return half a message.
        """
        raw = self._port.readline()  # bytes up to and including '\n'
        if not raw.endswith(b"\n"):  # timeout: nothing, or an incomplete line
            raise TimeoutError(f"no complete line from Teensy (got {raw!r})")
        # errors="replace" turns a corrupted byte into '?' instead of crashing;
        # parse_line() will then reject the line with a clear message.
        return raw.decode("ascii", errors="replace").rstrip("\r\n")

    def ping(self) -> SyncSample:
        """One PING/PONG round trip, timestamped on the host at both ends.

        The two clock readings sit as tightly as possible around the I/O:
        everything not part of the round trip (building the text, parsing
        the reply) happens outside them, so it does not inflate the RTT.

        A PONG with the wrong seq is a stale reply to an earlier ping that
        timed out; it is skipped. An ERR means the firmware rejected our
        PING, which is a bug, so it is raised.
        """
        seq = self._next_seq  # this ping's sequence number
        self._next_seq += 1  # the next one gets a fresh number
        data = format_ping(seq).encode("ascii")  # prepared before the clock starts

        send_ns = self._clock_ns()  # h1: just before the bytes go out
        self._port.write(data)
        self._port.flush()
        while True:  # read until OUR reply arrives (read_line times out if not)
            line = self.read_line()
            recv_ns = self._clock_ns()  # h2: just after a line arrived
            message = parse_line(line)  # raises ProtocolError on garbage
            if isinstance(message, Err):  # the firmware refused our PING
                raise ProtocolError(f"Teensy replied ERR {message.msg}")
            if isinstance(message, Pong) and message.seq == seq:  # ours
                return SyncSample(send_ns, recv_ns, message.t_us)
            # Otherwise: a stale PONG with an old seq. Loop and read again.

    def sync_clock(self, n: int = 20) -> ClockOffset:
        """Ping n times and return the minimum-round-trip offset estimate.

        At a few hundred microseconds per round trip, n = 20 takes only a
        few milliseconds - short enough that drift during it is negligible
        (20 ppm x 10 ms = 0.2 us).
        """
        if n < 1:  # zero samples cannot give an estimate
            raise ValueError(f"n must be >= 1, got {n}")
        samples = [self.ping() for _ in range(n)]  # n separate round trips
        return estimate_offset(samples)


def open(port_name: str, timeout_s: float = 1.0) -> Link:
    """Open the Teensy's serial port and return a ready Link.

    Named `open` so callers write `rhi.link.open("COM5")`. Inside this module
    it hides Python's built-in open(), which this module never needs.

    Three things happen after the port opens:
      1. Send a bare '\\n'. If a serial monitor left half a line in the
         firmware's buffer, this ends it, so our first PING starts clean.
         A truly empty line is silently ignored by HostLink (Step 2.3,
         Layer 3), so in the normal case nothing comes back.
      2. Wait 50 ms for any ERR that the half line provoked to arrive.
         (Waiting is fine on the host; the no-delay rule is for firmware.)
      3. Throw away everything received so far, so the first line we read
         is a reply to something we sent.

    The baud rate is nominal: the Teensy is native USB and ignores it.
    """
    port = serial.Serial(
        port_name,
        baudrate=115200,  # ignored by Teensy USB, required by pyserial
        timeout=timeout_s,  # readline() gives up after this many seconds
        write_timeout=timeout_s,  # write() too, instead of hanging forever
    )
    port.write(b"\n")  # 1. end any half-typed line on the Teensy
    port.flush()
    time.sleep(0.05)  # 2. let a possible ERR reply arrive
    port.reset_input_buffer()  # 3. discard it, and anything else stale
    return Link(port)


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------


def main(argv: Sequence[str] | None = None) -> int:
    """Entry point for `python -m rhi.link`. Returns the process exit code.

    --sync --repeat 3 runs three back-to-back syncs and reports whether they
    agree within their uncertainties (the Step 2.4 acceptance check). The
    syncs are only --interval seconds apart because drift between them must
    stay negligible: at 20 ppm, 1 s of drift is 20 us, well inside a typical
    +/- 200 us. With --repeat 2 --interval 60 the same command measures the
    drift instead.
    """
    parser = argparse.ArgumentParser(prog="python -m rhi.link")
    parser.add_argument("--port", required=True, help="e.g. COM5 or /dev/ttyACM0")
    parser.add_argument("--sync", action="store_true", help="estimate clock offset")
    parser.add_argument("-n", type=int, default=20, help="pings per sync")
    parser.add_argument("--repeat", type=int, default=1, help="number of syncs")
    parser.add_argument("--interval", type=float, default=1.0, help="s between")
    args = parser.parse_args(argv)  # None -> read sys.argv

    with open(args.port) as link:
        if not args.sync:  # no --sync: a single PING is a liveness check
            sample = link.ping()
            print(f"PONG t_us={sample.teensy_us}  RTT {sample.rtt_us:.1f} us")
            return 0

        offsets: list[ClockOffset] = []  # one entry per completed sync
        for i in range(args.repeat):
            if i > 0:  # wait between syncs, not before the first
                time.sleep(args.interval)
            o = link.sync_clock(args.n)
            offsets.append(o)
            print(
                f"sync {i + 1}: offset {o.offset_us:.1f} us "
                f"+/- {o.uncertainty_us:.1f} us "
                f"(min RTT {o.min_rtt_us:.1f} us, n={o.n})"
            )

    if len(offsets) > 1:  # the comparisons need at least two syncs
        first = offsets[0].offset_us
        rel = [wrap_diff_us(o.offset_us, first) for o in offsets]  # wrap-safe
        spread = max(rel) - min(rel)  # largest disagreement between syncs
        verdict = "agree" if offsets_agree(offsets) else "DISAGREE"
        print(f"spread {spread:.1f} us -> {verdict} within uncertainty")
        ppm, ppm_unc = drift_ppm(offsets[0], offsets[-1])
        span_s = (offsets[-1].host_us - offsets[0].host_us) / 1e6  # us -> s
        print(f"drift {ppm:+.1f} +/- {ppm_unc:.1f} ppm over {span_s:.1f} s")
    return 0


if __name__ == "__main__":  # run only via `python -m rhi.link`, not on import
    raise SystemExit(main())  # pass main()'s return value out as the exit code
