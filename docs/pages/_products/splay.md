---
title: Splay
order: 231
bundle: splay.vst3
kind: Audio effect
category: processor
group: space
role: Stereo matrix and image shaper
screenshot: /assets/plugins/splay.png
capabilities: [audio input, audio output]
summary: Route each input into either output, then set mid/side width, add a fractional right-channel image delay and invert the right channel. Defaults pass audio through untouched.
---

## Opinion

Splay is for the small stereo jobs that do not need a chorus or a reverb. It
answers three questions in order: where should each input go, how wide should
the result be, and should one channel arrive a little later? The four matrix
gains make routing explicit instead of hiding it behind a "pan" control, and
the defaults are a true pass-through, so it can be inserted without changing
the sound until a control is moved.

The width control is a plain mid/side gain, and Image Delay is a short,
fractional offset on the right channel only. Together they cover mono folding,
mono-to-stereo widening and Haas-style image shifts. Invert R is kept as a
separate switch because flipping a channel's phase is a different decision
from delaying it.

## Functionality

The matrix computes `L = inL x LL + inR x RL` and `R = inL x LR + inR x RR`.
Mid/side width then forms `M = (L + R) / 2` and `S = (L - R) / 2 x width`,
returning `L = M + S` and `R = M - S`. At 100 % width this stage is exactly
transparent. A fractional delay of up to 20 ms follows on the right channel,
then the optional invert, the output level and the bypass crossfade.

All continuous controls use 5 ms smoothing, including the bypass mix, so
changes and bypass toggles do not click. The first block uses the current
settings immediately. `activate()` and sample-rate changes clear only the
delay line; they never reset the patch.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| L to L | 0-200 % | 100 % | Left input into left output |
| L to R | 0-200 % | 0 % | Left input into right output |
| R to L | 0-200 % | 0 % | Right input into left output |
| R to R | 0-200 % | 100 % | Right input into right output |
| Width | 0-200 % | 100 % | 0 % mono, 100 % unchanged, 200 % double side |
| Image Delay | 0-20 ms | 0 ms | Right channel only; fractional interpolation |
| Invert R | Off, On | Off | Phase invert after the delay |
| Output | 0-200 % | 100 % | Final level on both channels |
| Bypass | Off, On | Off | Crossfade to the untouched input |

### Wiring

- **Audio:** 2 in, 2 out. Non-finite samples are replaced with zero; very
  small values are flushed to prevent denormals.
- **MIDI:** none. There is no MIDI input, output or CC map; automate the
  parameters in the host.
- **Transport:** ignored. Splay is a stateless audio effect.
- **Latency:** not reported. Image Delay is an intentional inter-channel
  offset and would be cancelled by host latency compensation. The 5 ms
  smoothing is applied to controls, not to the audio path.
- **State:** none. All nine settings are ordinary host parameters.

## Status

Core tests, the VST3 build, bundle wiring, the state audit and the reviewed
920x700 screenshot pass. Host listening in REAPER remains; see
`plugins/splay/docs/design.md` for the signal-path decisions and test coverage.
