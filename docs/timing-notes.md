# Timing notes

Running log of every timing number measured in this project. Each entry gets a
date, the conditions, and the instrument used.

## Host-Teensy clock synchronisation (step 2.4)

Command: `python -m rhi.link --port <COMx> --sync --repeat 3`. Uncertainty
is half the minimum round trip. The offset itself is arbitrary (it depends on
when each clock started), so what matters is the uncertainty and whether the
three runs agree.

| Date | n pings | Offset (us) | Uncertainty (us) | Min RTT (us) | Agree? | Notes |
|---|---|---|---|---|---|---|
| 2026-10-20 | 20 | -95751026059.2 | 291.6 | 583.2 | yes | run 1 of 3, Windows 11, COM3 |
| 2026-10-20 | 20 | -95751026046.1 | 285.8 | 571.5 | yes | run 2 of 3 |
| 2026-10-20 | 20 | -95751026032.5 | 278.5 | 557.0 | yes | run 3 of 3; spread 26.7 us |

Typical uncertainty: about +/- 280 us (min round trip about 560 us). The
three runs agree. A single ping outside a sync had a 743.7 us round trip,
which shows why we keep the minimum of 20. Separate processes
also agreed (about -95751026040 us), because perf_counter on Windows counts
from boot and is shared across processes.

### Clock drift

Command: `python -m rhi.link --port <COMx> --sync --repeat 2 --interval 60`.

| Date | Interval (s) | Drift (ppm) | +/- (ppm) | Notes |
|---|---|---|---|---|
| 2026-10-20 | 60 | -0.7 | 9.7 | consistent with zero; drift is below about 10 ppm |

What it means: at the worst case of 10 ppm, a 12 s trial drifts 120 us
(negligible), but a session drifts 0.6 ms per minute. Plan: re-sync before
every trial (about 20 ms) when the session loop is built (Step 6.10). A longer
interval (for example 600 s) would narrow the bound if we ever need it.
