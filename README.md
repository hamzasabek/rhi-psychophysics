# RHI psychophysics rig

Robotic tactile stimulation apparatus for rubber hand illusion psychophysics,
replicating the two-arm setup of Chancel, Ehrsson & Ma (eLife 2022) and
Chancel & Ehrsson (Cortex 2023).

Two robot arms deliver taps to a participant's hidden right index finger and to
the index finger of a visible rubber hand, with a programmable asynchrony
between them. Participants judge whether the rubber hand felt like their own.

- **Firmware** (`firmware/`) — Teensy 4.1, C++, PlatformIO. Owns all timing.
- **Host** (`host/`) — Python 3. Owns experimental design, audio, visual noise,
  and data logging.
- **Specification and roadmap** — see `PROJECT.md`.

## Quick start

```bash
# firmware
cd firmware && pio run -e teensy41 && pio run -t upload

# host
python -m venv host/.venv
source host/.venv/bin/activate      # Windows: host\.venv\Scripts\activate
pip install -e "host[dev]"
pytest host/tests
```

Open `rhi.code-workspace` in VSCode rather than the folder itself.
