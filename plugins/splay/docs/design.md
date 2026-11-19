# Splay design notes

Splay fills the stereo matrix/enhancer gap identified in `docs/lynx-survey.md`.
Chorale widens with chorus, Glue and Trio are dynamics tools, and Octet can work
in mid/side, but none offers a four-gain routing matrix followed by a plain
width control and a fractional inter-channel delay. This is a clean-room
implementation: no upstream code, graphics or presets were copied.

## Signal path

```
l = inL x LL + inR x RL
r = inL x LR + inR x RR

m = 0.5 (l + r)
s = 0.5 (l - r) x width
l, r = m + s, m - s

r = fractionalDelay(r, imageDelay)   // right channel only
r = r x (invertR ? -1 : +1)
l, r = l x level, r x level

l = l x (1 - bypass) + inL x bypass
r = r x (1 - bypass) + inR x bypass
```

The delay line is 4096 samples. The largest requested offset is 20 ms, which
is 3840 samples at 192 kHz, so the extra 256 samples cover the interpolation
read and keep the ring index simple. Whole-sample delay chooses the tap;
the remainder linearly interpolates with the previous sample.

## Decisions

1. **Matrix before width.** This keeps the four gains legible as "where does
   each input go", and makes width act on the routed pair rather than on the
   sources. At width 100 % the mid/side stage is exactly transparent.
2. **Width 0 % is mono, 200 % doubles the side.** Both channels receive the
   same mid component; only the side is scaled. No extra mono-compatibility
   processing or anti-phase cancellation is applied.
3. **Delay is right-channel only and is not host latency.** The offset is the
   effect. Reporting it as latency would let the host align the channels again
   and cancel it. Invert R is applied after the delay, so a delayed, inverted
   right channel stays inverted.
4. **Bypass is a crossfade.** `Invert R`, `Output`, width and delay are all
   smoothed with the same 5 ms exponential coefficient, including the bypass
   mix, so switching Bypass does not click.
5. **The first block snaps; activation clears runtime only.** A fresh engine
   starts with its smoothed values set to the current patch, so the default is
   bit-exact pass-through and editing a parameter before the first block does
   not fade in from zero. `reset()` (called by `activate()` and
   `sampleRateChanged()`) clears the delay line and fault counter but never
   touches the parameters held by the DPF wrapper.
6. **Non-finite samples are isolated.** Each input sample is checked before
   routing; NaN or infinity becomes zero and increments an internal fault
   counter. Outputs below `1e-20` are flushed to zero to stop recursive
   denormal costs. Nothing is exposed to the host for either behaviour.
7. **No state, MIDI or transport.** Nine ordinary parameters carry everything;
   the host saves them. There are no status outputs and no CC map.

## Parameters and state

| Symbol | Name | Range | Default | Meaning |
|---|---|---|---|---|
| `matrixLL` | L to L | 0-2 | 1 | Left input gain into left output |
| `matrixLR` | L to R | 0-2 | 0 | Left input gain into right output |
| `matrixRL` | R to L | 0-2 | 0 | Right input gain into left output |
| `matrixRR` | R to R | 0-2 | 1 | Right input gain into right output |
| `width` | Width | 0-2 | 1 | Mid/side side gain |
| `imageDelay` | Image Delay | 0-20 ms | 0 | Fractional right-only delay |
| `invertR` | Invert R | 0/1 | 0 | Phase invert on the right channel |
| `level` | Output | 0-2 | 1 | Final output gain |
| `bypass` | Bypass | 0/1 | 0 | Crossfade to untouched input |

`clampParameters()` is shared by the wrapper and the core: non-finite values
fall back to the spec default, ranges are clamped and integer switches are
rounded. The DPF wrapper uses `Plugin(parameterCount, 0, 0)`, so no state is
offered to the host.

## Tests (`tests/splay_core_tests.cpp`)

- Default settings are bit-exact pass-through across a rendered tone.
- All four matrix gains route their source to the expected output, and the
  unity-width stage is transparent.
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
- Every control changes the rendered output from a non-default reference patch.

## Not done

Listening and host validation (REAPER) are not yet complete. The 920x700 panel
passed visual review and is in `docs/pages/assets/plugins/splay.png`.
