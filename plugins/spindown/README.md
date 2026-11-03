# Spindown

A tape-stop effect: the tape slows to a standstill, pitch and time falling together, then spins back up and rejoins the live
signal. Trigger it by hand, from held MIDI notes, or automatically on a bar cycle locked to the host. Design and assumptions
are in [docs/design.md](docs/design.md).

| Control | Range | Default | Notes |
|---|---|---|---|
| Trigger | Manual, MIDI, Cycle | Manual | What stops the tape |
| Stop | Playing, STOPPED | Playing | Manual trigger: automate this. CC 4 (64+) sets it |
| Stop Time | 20-4000 ms (log) | 800 ms | Time from full speed to standstill. CC 1 |
| Start Time | 20-4000 ms (log) | 300 ms | Time from standstill to full speed. CC 2 |
| Curve | 0.5-4 | 1.5 | Speed is `(1 - t/T)^Curve`: 1 is a straight ramp, higher drops early and lingers, lower holds speed then drops late. CC 3 |
| Cycle | 1, 2, 4, 8 bars | 2 bars | Cycle trigger: the length of the repeat |
| Length | 0.25-16 beats | 2 beats | Cycle trigger: the tape is stopped for the last Length beats of each cycle |
| CC Channel | 0 (off), 1-16 | 1 | MIDI channel for CC 1-4 |

Audio: stereo in, stereo out. MIDI in (notes for the MIDI trigger, CC 1-4 on the CC channel; nothing is passed on). No latency:
with the effect idle the output is the input, sample for sample. Settings are ordinary automatable parameters; there is no
plugin state. Two output parameters (`status_speed`, `status_delay`) feed the panel.
