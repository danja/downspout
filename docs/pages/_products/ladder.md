---
title: Ladder
order: 218
bundle: ladder.vst3
kind: Audio effect
category: processor
role: Low-pass ladder filter
screenshot: /assets/plugins/ladder.png
capabilities: [audio input, audio output]
summary: A four-pole saturating low-pass ladder with self-oscillating resonance, a bass-compensation control, drive, mix and output, and a live plot of its response.
---

## Opinion

Not yet auditioned in a host. It fills the gap between Skream (a scream filter built on an SVF
with a feedback path) and the distortions: a plain, steep, resonant low-pass that rings and
saturates the way a transistor ladder does. Expect it to be most useful on synths and basses,
swept by automation.

## Functionality

Ladder is a stereo low-pass filter made of four cascaded integrators with the output fed back
to the input, a tanh saturator at each stage, and 2x oversampling. It is a port of the
zero-delay-feedback ladder in the Oxide Studio DSP, measured before porting: with no resonance
its response matches four cascaded one-poles to within 0.01 dB (-12 dB at the cutoff, -3 dB at
about 0.44 of it), and at full resonance it rings at the cutoff, within 0.5 %, without
blowing up.

### Parameters

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Cutoff | 20-18000 Hz, logarithmic | 1 kHz | |
| Resonance | 0-100 % | 20 % | Peak over the passband (with full Bass Comp) of about 6 dB at 20 %, 18 dB at 50 %, 28 dB at 70 %. Rings from about 85 % |
| Drive | 0-24 dB | 0 dB | Gain into the saturators. Not shown in the plot |
| Bass Comp | 0-100 % | 50 % | Feedback removes low end; this puts it back. 100 % restores the passband to 0 dB |
| Mix | 0-100 % | 100 % | Dry/wet |
| Output | -24 to +12 dB | 0 dB | |

### Two choices that differ from the source

- **Resonance taper.** The original maps the control with a power law that puts the peak at
  about +1 dB at 50 % but +30 dB at 90 %, so nearly all the useful range is in the last tenth.
  Ladder uses an exponential taper in the feedback amount, which spreads the peak height
  roughly evenly across the control.
- **Bass compensation.** Resonance at 50 % costs about 9.5 dB at 100 Hz, and the original
  restores only a little of it. Ladder makes it a control.

### The plot

The response curve is drawn from a linear model (four one-poles in a feedback loop, plus Bass
Comp, Mix and Output). The tests hold it within 0.4 to 2.5 dB of the measured filter across the
passband, the slope and the resonant peak. Drive is non-linear and is not plotted. Below
-48 dB the curve is not drawn.

### Status

Core covered by tests (analytic response, taper, self-oscillation, bass compensation, stability
under random sweeps and overload at 44.1 to 192 kHz, block-size independence, denormals, and the parameter table against the core). Not yet listened to or validated in a host.
