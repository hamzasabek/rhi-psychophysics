"""Host-side control and analysis for the RHI psychophysics rig.

The host owns experimental design, randomisation, audio, the projected visual
noise field, and all data logging. It does NOT own timing: every timestamp
that matters is produced by the Teensy firmware, which has no operating system
to interrupt it. See PROJECT.md section 4.2.
"""

__version__ = "0.2.0"
