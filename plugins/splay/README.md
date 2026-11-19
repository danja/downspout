# Splay

Splay is a stereo matrix and image shaper. Each input can feed either output
through its own gain; the routed pair then passes through mid/side width, an
optional fractional delay on the right channel, a right-channel phase invert,
output level and a click-free bypass crossfade. The defaults are a bit-exact
stereo pass-through.

```
in L/R -> 4-way matrix -> mid/side width -> delay R -> invert R -> level -> out L/R
```

| Control | Range | Default | Notes |
|---|---|---|---|
| L to L | 0-200 % | 100 % | Left input into left output |
| L to R | 0-200 % | 0 % | Left input into right output |
| R to L | 0-200 % | 0 % | Right input into left output |
| R to R | 0-200 % | 100 % | Right input into right output |
| Width | 0-200 % | 100 % | 0 % is mono, 100 % unchanged, 200 % doubles the side |
| Image Delay | 0-20 ms | 0 ms | Fractional delay applied only to the right channel |
| Invert R | Off / On | Off | Flips the right channel's phase after the delay |
| Output | 0-200 % | 100 % | Final level on both channels |
| Bypass | Off / On | Off | Crossfades to the untouched input |

The matrix runs before the width stage, so routing decisions change what the
width control acts on. Width uses `M = (L + R) / 2` and `S = (L - R) / 2`, then
returns `L = M + S x width` and `R = M - S x width`. Image Delay is an audible
inter-channel offset (a Haas-style image control), not processing latency, so
it is not reported to the host. All continuous controls use 5 ms smoothing;
the first block snaps to the current settings, and the delay line is cleared
only by `activate()` or a sample-rate change, never by a parameter edit.

Stereo in and out, no MIDI, no transport and no plugin state. Every control is
an ordinary automatable parameter saved by the host. Non-finite input samples
are replaced with zero and tiny values are flushed to avoid denormals.

Status: portable core tests, the VST3 build, bundle wiring, the state audit and
the reviewed 920x700 screenshot pass. Host listening remains; see
`docs/design.md` for the signal-path decisions and the remaining validation gap.
