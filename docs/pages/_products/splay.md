---
title: Splay
order: 231
bundle: splay.vst3
kind: Audio effect
category: processor
group: space
role: Stereo matrix and image shaper
screenshot: /assets/plugins/splay.png
capabilities: [audio input, audio output, MIDI CC input]
summary: Route each input into either output, then shape rotation, pan law, mid/side width, frequency-dependent width and inter-channel delay; CC 1-4 animate rotation, width, mid and delay, and read-only meters report correlation and mono fit.
---

## Opinion

Splay is for the small stereo jobs that do not need a chorus or a reverb. It
answers four questions in order: where should each input go, how should the
stereo image be turned, how wide should the result be, and should one channel
arrive a little later? The four matrix gains make routing explicit instead of
hiding it behind a "pan" control, and the defaults are a true pass-through, so
it can be inserted without changing the sound until a control is moved.

Rotation, Pan Law, Mid, M/S Solo and the two-band Spectral Width turn the
original matrix/width tool into a focused stereo enhancer without transport or
state. CC 1-4 on the selected channel move Rotation, Width, Mid and Image Delay,
so a controller can animate the image while the other controls stay put. The
correlation and mono-fit meters read the final outputs, so the panel shows the
result of bypass and inversion as well as the patch.

## Functionality

The matrix computes `L = inL x LL + inR x RL` and `R = inL x LR + inR x RR`.
Pan Law optionally normalises only matrix columns and rows with more than one
active path. Rotation then mixes the routed pair; 0 degrees is exactly
transparent. Mid/side width forms `M = (L + R) / 2` and `S = (L - R) / 2`, with
independent Mid and Width gains, and M/S Solo isolates one component. With
Spectral Width on, a one-pole crossover applies separate low and high side
factors and blends that result over the plain stage. A fractional delay of up
to 20 ms follows on the right channel, then the optional invert, output level
and bypass crossfade.

All continuous controls use 5 ms smoothing, including the bypass mix, so
changes and bypass toggles do not click. The first block uses the current
settings immediately. `activate()` and sample-rate changes clear only runtime
state; they never reset the patch.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| L to L | 0-200 % | 100 % | Left input into left output |
| L to R | 0-200 % | 0 % | Left input into right output |
| R to L | 0-200 % | 0 % | Right input into left output |
| R to R | 0-200 % | 100 % | Right input into right output |
| Rotation | -90 to +90 deg | 0 deg | Post-matrix stereo rotation |
| Pan Law | Linear, -3 dB | Linear | Conditional constant-power matrix compensation |
| Width | 0-200 % | 100 % | 0 % mono, 100 % unchanged, 200 % double side |
| Mid | 0-200 % | 100 % | Independent mid gain |
| M/S Solo | Off, Mid, Side | Off | Isolate one mid/side component |
| Spectral Width | Off, On | Off | Enable the two-band side gains |
| Crossover | 100-2000 Hz | 500 Hz | One-pole low/high split |
| Low Width | 0-200 % | 100 % | Low-band side factor |
| High Width | 0-200 % | 100 % | High-band side factor |
| Image Delay | 0-20 ms | 0 ms | Right channel only; fractional interpolation |
| Invert R | Off, On | Off | Phase invert after the delay |
| Output | 0-200 % | 100 % | Final level on both channels |
| Bypass | Off, On | Off | Crossfade to the untouched input |
| CC Channel | Off, 1-16 | 1 | MIDI channel that accepts CC 1-4; Off ignores MIDI |
| Correlation | -1 to +1 | 0 | Read-only, 20 ms smoothed final-output meter |
| Mono Fit | 0-100 % | 0 | Read-only, 20 ms smoothed final-output meter |
| Rotation (CC1) | -90 to +90 deg | 0 | Read-only effective value after CC override |
| Width (CC2) | 0-200 % | 100 % | Read-only effective value after CC override |
| Mid (CC3) | 0-200 % | 100 % | Read-only effective value after CC override |
| Image Delay (CC4) | 0-20 ms | 0 ms | Read-only effective value after CC override |

The original nine parameters keep their indices; the new controls and meters
were appended to preserve existing host automation.

### Wiring

- **Audio:** 2 in, 2 out. Non-finite samples are replaced with zero; very
  small values are flushed to prevent denormals.
- **MIDI:** one input; no output. **CC Channel** (0 = off, default 1) selects
  the channel. CC 1 drives Rotation (-90 to +90 deg), CC 2 Width (0-200 %),
  CC 3 Mid (0-200 %) and CC 4 Image Delay (0-20 ms). A controller overrides its
  destination until the panel or host moves that parameter again; the four
  `outCc*` outputs report the effective values.
- **Transport:** ignored. Splay is a stateless audio effect.
- **Latency:** not reported. Image Delay is an intentional inter-channel
  offset and would be cancelled by host latency compensation. The crossover is
  a tone-shaping filter, and the 5 ms smoothing applies to controls, not to
  audio alignment.
- **State:** none. All eighteen writable settings are ordinary host
  parameters; the two meters and four live CC mirrors are output-only.

## Status

Core tests, the clean VST3 build and the reviewed CC-labelled 920x700 screenshot
pass; the five image enhancements also passed an initial REAPER listening check.
CC motion still needs a host listening pass. See `plugins/splay/docs/design.md`
for the signal-path decisions and test coverage.
