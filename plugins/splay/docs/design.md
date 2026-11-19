# Splay design notes

Splay fills the stereo matrix/enhancer gap identified in `docs/lynx-survey.md`.
Chorale widens with chorus, Glue and Trio are dynamics tools, and Octet can work
in mid/side, but none offers a four-gain routing matrix followed by explicit
image controls. This is a clean-room implementation: no upstream code, graphics
or presets were copied.

## Signal path

```
g = panLaw ? constantPower(matrix gains) : matrix gains
l = inL x g.LL + inR x g.RL
r = inL x g.LR + inR x g.RR

rotL = l cos(theta) - r sin(theta)
rotR = l sin(theta) + r cos(theta)

plain  = midSide(rotL, rotR, midGain, width, msSolo)
low    = lowpass(rotL, rotR, crossover)
high   = rot - low
shaped = plain + spectralWidth x (midSide(low, high gains) - plain)

r = fractionalDelay(shaped.R, imageDelay)
r = r x (invertR ? -1 : +1)
l, r = l x level, r x level

l = l x (1 - bypass) + inL x bypass
r = r x (1 - bypass) + inR x bypass
```

`midSide(x, y, mid, side, solo)` forms `M = 0.5 (x + y) x mid` and `S = 0.5 (x
- y) x side`, returning `M +/- S`, or the mid/side component when solo is
active. The delay line is 4096 samples; 20 ms is 3840 samples at 192 kHz, so the
extra headroom covers the interpolation read. Whole-sample delay chooses the tap
and the remainder linearly interpolates with the previous sample.

## Decisions

1. **The original nine parameters keep their indices.** Rotation, Pan Law, Mid,
   M/S Solo, Spectral Width, Crossover, Low Width and High Width were appended,
   as were the two read-only outputs. Existing host automation keeps its mapping.
2. **Matrix before rotation and width.** The four gains stay legible as "where
   does each input go", and every later stage acts on the routed pair. At the
   defaults the matrix is unity and rotation is zero, so the whole chain is
   bit-exact pass-through.
3. **Pan law is conditional.** A row or column is normalized by the Euclidean
   norm of its gains only when it has more than one strictly positive path. A
   single boosted path remains deliberate gain, so the switch never cancels a
   user's boost. Linear is the default to preserve the original plugin.
4. **Rotation is post-matrix.** It rotates the stereo pair rather than changing
   routing; 0 degrees is exactly transparent because the sine is exactly zero,
   the cosine exactly one, and the same floating-point operations are retained.
   The panel's Swap L/R action sets the four matrix gains for a 90-degree
   channel swap; Reset restores the four gains only.
5. **Mid and Side have independent trims.** Global Width remains the master
   side gain; Mid is a separate gain. M/S Solo crossfades to Mid or Side, so
   both settings affect what is heard during solo and the side stage remains
   audible in the default stereo output.
6. **Spectral width is a tone control, not a latency tool.** A one-pole
   low-pass and its complementary high band split the rotated signal. Each band
   gets `width x lowWidth` or `width x highWidth` as its side gain, then the
   difference from the plain stage is blended in by Spectral Width. The blend
   is off by default and reports no host latency.
7. **Delay is right-channel only and is not host latency.** The offset is the
   effect; reporting it would let the host align the channels again and cancel
   it. Invert R is applied after the delay, so a delayed, inverted right channel
   stays inverted.
8. **Bypass is a crossfade.** All continuous controls and the bypass mix use the
   same 5 ms exponential coefficient, so toggling bypass does not click.
9. **The first block snaps; activation clears runtime only.** A fresh engine
   starts with its smoothed values set to the current patch. `reset()` clears
   the delay line, crossover memory, meters and fault counter but never touches
   the parameters held by the DPF wrapper.
10. **Meters read the final outputs.** Correlation is `(M^2 - S^2) / (M^2 + S^2)`
    and Mono Fit is `2 |M| / (|L| + |R|)`, smoothed over 20 ms. A silent frame
    reads zero. Inversion and bypass are included, so a bypassed patch reports
    the untouched input.
11. **Bad samples are isolated.** Each input sample is checked before routing;
    NaN or infinity becomes zero and increments an internal fault counter.
    Outputs below `1e-20` are flushed to zero to stop recursive denormal costs.
12. **No state, MIDI or transport.** Seventeen ordinary writable parameters and
    two read-only meters carry everything; the host saves the writable ones.

## Parameters and state

| Symbol | Name | Range | Default | Meaning |
|---|---|---|---|---|
| `matrixLL` | L to L | 0-2 | 1 | Left input gain into left output |
| `matrixLR` | L to R | 0-2 | 0 | Left input gain into right output |
| `matrixRL` | R to L | 0-2 | 0 | Right input gain into left output |
| `matrixRR` | R to R | 0-2 | 1 | Right input gain into right output |
| `rotation` | Rotation | -90 to +90 deg | 0 | Post-matrix stereo rotation |
| `panLaw` | Pan Law | 0/1 | 0 | Conditional constant-power compensation |
| `width` | Width | 0-2 | 1 | Master mid/side side gain |
| `midGain` | Mid | 0-2 | 1 | Independent mid gain |
| `msSolo` | M/S Solo | 0/1/2 | 0 | Off, mid only, side only |
| `spectralWidth` | Spectral Width | 0/1 | 0 | Blend in the two-band side gains |
| `crossover` | Crossover | 100-2000 Hz | 500 | One-pole split frequency |
| `lowWidth` | Low Width | 0-2 | 1 | Low-band side-gain factor |
| `highWidth` | High Width | 0-2 | 1 | High-band side-gain factor |
| `imageDelay` | Image Delay | 0-20 ms | 0 | Fractional right-only delay |
| `invertR` | Invert R | 0/1 | 0 | Phase invert on the right channel |
| `level` | Output | 0-2 | 1 | Final output gain |
| `bypass` | Bypass | 0/1 | 0 | Crossfade to untouched input |
| `outCorrelation` | Correlation | -1 to 1 | 0 | Read-only final-output correlation |
| `outMono` | Mono Fit | 0-1 | 0 | Read-only final-output mono fit |

`clampParameters()` is shared by the wrapper and the core: non-finite values
fall back to the spec default, ranges are clamped and integer switches are
rounded. The DPF wrapper uses `Plugin(parameterCount, 0, 0)`, so no state is
offered to the host. Output parameters are read-only in `setParameterValue()`.

## Tests (`tests/splay_core_tests.cpp`)

- Default settings are bit-exact pass-through across a rendered tone.
- All four matrix gains route their source to the expected output.
- Width 0/0.5/1/2 produces the expected mono and left-only mid/side results.
- A 10 ms impulse lands on the expected sample at 44.1 and 48 kHz; a
  half-sample delay splits the impulse between two neighbours.
- Invert R and Output affect only the intended channels/gains.
- Bypass is inert at its default and exactly passes the input when on, even
  with other controls changed.
- Block-size independence at sizes 1, 7 and 256, plus sample-rate scaling of
  the image delay.
- Non-finite input is counted and silenced; wild parameter values clamp to
  finite, in-range values.
- `reset()` clears the delay line but leaves the patch effective.
- Tiny values are flushed to zero.
- Rotation by +/-90 swaps the channels with the expected sign, and Pan Law
  normalises duplicate paths without touching a single boosted path.
- Mid gain and M/S Solo isolate the expected components.
- Spectral Width leaves a DC mid unchanged and boosts only the alternating
  high band with the expected low/high factors.
- Correlation reads +1 for identical channels and -1 for antiphasal channels;
  Mono Fit reads 1 and 0 respectively, bypass reports the untouched input, and
  both reset to zero with the runtime state.
- Every writable control changes the rendered output from a non-default
  reference patch.

## Not done

The expanded panel passed user visual review at 920x700 and is in
`docs/pages/assets/plugins/splay.png`. Host listening after the five additions
is pending.
