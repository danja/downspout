---
title: Gate
order: 232
bundle: gate.vst3
kind: Audio effect
category: processor
group: dynamics
role: Linked stereo noise gate with sidechain
screenshot: /assets/plugins/gate.png
capabilities: [audio input, audio output]
summary: A clean-room stereo noise gate with internal or external sidechain, sidechain high-pass, hysteresis, hold, attack/release shaping, range, output trim and live detector, reduction and gate-status meters.
---

## Opinion

Gate is the plain, dependable noise gate Downspout was missing. It does not try
to be a vocal strip or a keyed effect: one RMS-like detector opens one gain that
is shared by both main channels, and the whole job is visible in the panel. The
defaults pass a healthy signal untouched and attenuate a quiet one by 60 dB.
Select External when another track should key the gate; if nothing is connected
there, the gate closes instead of silently falling back to the main inputs.

## Functionality

The detector listens to main L/R or inputs 3/4, with an optional one-pole
high-pass to keep rumble out of the decision. Its mean-square level has a 2 ms
rise and 40 ms fall to resist chatter. A level above Threshold opens the gate;
an open gate closes only after the level drops below `Threshold - Hysteresis`,
waits Hold, and then fades over Release. The shared open amount maps to
`10^(-Range x (1 - open) / 20)`, so the range is the maximum attenuation while
closed. Output is a post-gate trim and Bypass crossfades over 5 ms.

The live history strip draws the detector against the open and close points and
marks the samples where the gate state is open. Detector, Reduction and Gate are
read-only outputs measured from the audio thread.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Threshold | -80 to 0 dB | -40 dB | Detector level that opens the gate |
| Hysteresis | 0 to 20 dB | 6 dB | Closes at `Threshold - Hysteresis` |
| Range | 0 to 80 dB | 60 dB | Closed attenuation; 0 dB is transparent |
| Attack | 0 to 100 ms | 5 ms | Shared gain opening time |
| Hold | 0 to 500 ms | 50 ms | Counted only below the close point |
| Release | 10 to 2000 ms | 200 ms | Shared gain closing time |
| SC Source | Internal / External | Internal | External uses inputs 3/4 |
| SC HPF | 20 to 500 Hz | 20 Hz | Detector only; 20 Hz is off |
| Output | -12 to 12 dB | 0 dB | Post-gate trim |
| Bypass | Off / On | Off | 5 ms crossfade to the main input |
| Detector | -100 to 6 dB | -100 dB | Read-only smoothed sidechain level |
| Reduction | -80 to 0 dB | 0 dB | Read-only applied gate gain |
| Gate | 0 or 1 | 0 | Read-only open state, before tonal smoothing |

### Wiring

- **Audio:** 4 in (main L/R, sidechain L/R), 2 out (gated main L/R). DPF exposes
  inputs 3/4 as an auxiliary sidechain bus to hosts that support it.
- **MIDI:** none.
- **Transport:** ignored. Gate has no position or tempo dependency.
- **Latency:** none; the gate is sample-aligned.
- **State:** none. All ten writable controls are ordinary host parameters; the
  Detector, Reduction and Gate outputs are read-only.

Not yet auditioned in a host. Core tests cover pass-through, gating, the
external sidechain, hysteresis, attack/hold/release, stereo linking, bypass,
output gain, `reset()` and block-size invariance. The 920x700 screenshot was
captured and checked for size and non-blank pixels, but visual acceptance is
**pending**: this agent cannot inspect images at full resolution in this
session. See `plugins/gate/docs/design.md` for the design and test notes.
