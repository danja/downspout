---
title: Spense
order: 227
bundle: spense.vst3
kind: Audio effect
category: processor
role: Phase disperser
screenshot: /assets/plugins/spense.png
capabilities: [audio input, audio output]
summary: A stereo phase disperser: up to eight cascades of eight all-pass filters that delay frequencies near a chosen centre without changing level, with a live group-delay plot.
---

## Opinion

Not yet auditioned in a host. A phase disperser is a specialist tool: it changes how a sound is
laid out in time, not how loud or bright it is, so it is most useful on transients (kicks, snares,
clicks, plucks) and on sounds you want to widen or soften without touching their level. Reach for
Ladder or Skream when you want to change the tone instead.

## Functionality

Spense is a stereo effect built from all-pass filters. An all-pass passes every frequency at unity
gain but delays some more than others; stacking many of them near one frequency delays that region
by several milliseconds, which smears a sharp transient into a short sweep. It is a port of the DSP
in the Dispenser plugin (MIT): eight second-order trapezoidal state-variable all-pass stages per
cascade, up to eight cascades in series, with Drift fanning the stages apart in frequency
differently on the left and right.

### Parameters

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Stack | 1-8 | 1 | Cascades in series, eight stages each. Latency is 7 samples per cascade, reported to the host |
| Frequency | 40-20000 Hz, logarithmic | 1 kHz | Centre of the dispersion |
| Resonance | 0.1-10 Q, logarithmic | 0.707 | Low is a broad gentle smear, high a narrow long one |
| Drift | 0-1 | 0 | Spreads the stages in frequency, differently left and right |
| Pre Gain | -100 (silence) to +12 dB | 0 dB | Before the stack |
| Post Gain | -100 (silence) to +12 dB | 0 dB | After the stack |
| Clip at 0 dB | Off / On | Off | Hard clip after Post Gain |

### The plot

The curve is the stack's group delay in milliseconds against frequency, from the same analytic model
the tests hold to the measured filter (phase within 0.04 rad, group delay within 4 % + 0.6 samples).
With Drift above zero the left channel is drawn bright and the right faint. At low Q the delay peaks
below the Frequency marker (at 0.64 x for Q 0.707); by Q 5 it sits on it.

### Differences from the source

Coefficients are computed once per sample instead of once per cascade (no change to the output, far
cheaper: stack 8 is about 2.5 % of one core), the first block does not fade in, switched-off cascades
are cleared, and Resonance is logarithmic. Details in the plugin's `docs/design.md`.

### Status

Core covered by tests, including the source algorithm written out literally. Not yet listened to or
validated in a host.
