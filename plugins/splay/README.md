# Splay

Splay is a stereo matrix and image shaper. Each input can feed either output
through its own gain; the routed pair can be rotated, compensated for pan law,
trimmed in mid/side, widened differently in two frequency bands, delayed on the
right channel, inverted, levelled and bypassed. CC 1-4 on a selected MIDI
channel can animate Rotation, Width, Mid and Image Delay. The defaults are a
bit-exact stereo pass-through.

```
in L/R -> matrix (+ pan law) -> rotation -> mid/side trims + width
       -> optional two-band width -> delay R -> invert R -> level -> out L/R
```

| Control | Range | Default | Notes |
|---|---|---|---|
| L to L | 0-200 % | 100 % | Left input into left output |
| L to R | 0-200 % | 0 % | Left input into right output |
| R to L | 0-200 % | 0 % | Right input into left output |
| R to R | 0-200 % | 100 % | Right input into right output |
| Rotation | -90 to +90 deg | 0 deg | Stereo rotation after the matrix; 0 is exactly transparent |
| Pan Law | Linear / -3 dB | Linear | Compensates only rows or columns with multiple active matrix paths |
| Width | 0-200 % | 100 % | Master mid/side side gain: 0 % mono, 100 % unchanged, 200 % double |
| Mid | 0-200 % | 100 % | Independent mid gain |
| M/S Solo | Off / Mid / Side | Off | Listen to one mid/side component |
| Spectral Width | Off / On | Off | Enables frequency-dependent side gains |
| Crossover | 100-2000 Hz | 500 Hz | One-pole low/high split for Spectral Width |
| Low Width | 0-200 % | 100 % | Side-gain factor below the crossover |
| High Width | 0-200 % | 100 % | Side-gain factor above the crossover |
| Image Delay | 0-20 ms | 0 ms | Fractional delay applied only to the right channel |
| Invert R | Off / On | Off | Flips the right channel's phase after the delay |
| Output | 0-200 % | 100 % | Final level on both channels |
| Bypass | Off / On | Off | Crossfades to the untouched input |
| CC Channel | Off / 1-16 | 1 | MIDI channel that accepts CC 1-4; Off ignores MIDI |

| CC | Destination | Range |
|---|---|---|
| 1 | Rotation | -90 to +90 deg |
| 2 | Width | 0-200 % |
| 3 | Mid | 0-200 % |
| 4 | Image Delay | 0-20 ms |

A controller overrides its destination until the panel or host moves that
parameter again; the four `outCc*` parameters report the effective values so the
panel can show live movement.

The **Correlation** and **Mono Fit** outputs are read-only meters. Both read the
final outputs, including inversion and bypass, and are smoothed over 20 ms so
the panel can show them.

The original nine controls keep their parameter indices; the eight new controls,
CC Channel and the six read-only outputs were appended so existing host
automation still maps correctly.
Width uses `M = (L + R) / 2` and `S = (L - R) / 2`, then returns `L = M + S x
width` and `R = M - S x width`. With Spectral Width on, the same operation is
applied separately to a one-pole low band and its complementary high band:
Low Width and High Width multiply the master Width only in their own band. No
latency is reported because the split is a tone-shaping filter, not an alignment
tool.

Continuous controls use 5 ms smoothing. The first block snaps to the current
settings, the defaults stay bit-exact, and `activate()` or a sample-rate change
clears runtime state, including CC overrides, without resetting the patch.
Stereo in/out and one MIDI input for CC 1-4; no transport and no plugin state.
All eighteen writable settings are ordinary automatable parameters saved by the
host. Non-finite input samples are replaced with zero and tiny values are
flushed to avoid denormals.

See `docs/design.md` for the signal-path decisions and test coverage.
