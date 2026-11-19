---
title: Tilt
order: 246
bundle: tilt.vst3
kind: Audio effect
category: processor
group: tone
role: Master-bus tone control
screenshot: /assets/plugins/tilt.png
capabilities: [audio input, audio output]
summary: Linked stereo tilt with a straight-line dB response against log frequency, a draggable Pivot Freq and Angle, selectable 6/12/24 dB per octave, switchable minimum-phase IIR or linear-phase FFT, and a live spectrum behind the response curve.
---

## Opinion

Tilt is for the last small move on a mix: a little more air, a little less weight,
without redrawing an EQ curve by hand. One pivot and one angle define the whole
response, so the control is easy to explain and easy to automate. The line is
clamped to +/-24 dB at the ends, which keeps a full-scale setting deliberately
strong rather than absurd on a master bus. Defaults are inert, so inserting it
changes nothing.

The linear-phase option is the reason to reach for Tilt over a general EQ. It
applies an exact gain line to the spectrum with no phase distortion, at the cost
of 4096 samples of latency that the host is told about. The minimum-phase option
has zero latency for tracking and live use. Both act identically on left and
right; there are no independent trims to unbalance the image.

## Functionality

The ideal gain is `clamp(Angle x Slope x log2(f / Pivot), -24, +24) dB`. With the
default Slope of 6 dB/oct, Angle +1 lifts 20 kHz by 24 dB relative to a 1 kHz
pivot and cuts 20 Hz by the same amount. Drag the plot across to move Pivot Freq
and up or down to set Angle; scroll over the plot for fine angle adjustments.
The faint line is the target law and the bright curve is the response actually
used by the audio path.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Pivot Freq | 20 Hz-20 kHz | 1 kHz | Logarithmic; the unity point of the tilt |
| Angle | -1 to +1 | 0 | Effective slope is `Angle x Slope` dB/oct |
| Slope | 6 / 12 / 24 dB | 6 dB | Full-scale tilt per octave |
| Mode | IIR / FFT | IIR | Minimum phase, zero latency / linear phase, 4096 samples |
| Output | -12 to +12 dB | 0 dB | Applied after the tilt |
| Bypass | Off / On | Off | Crossfades to the untouched input |
| Spectrum | Off / Pre / Post | Post | Analyser tap behind the response curve |
| Latency | 0-200 ms | 0 ms | Read-only, from the active Mode |
| Peak L / R | -60 to +6 dB | -60 dB | Read-only output meters |

There are also 64 read-only `spectrum_NN` levels from 20 Hz to 20 kHz.

### Processing

IIR mode cascades up to four whole 6 dB/oct shelves for the integer part of the
exponent, then a six-section Oustaloup ladder for the fractional remainder. The
shelves span the frequencies where the ideal line reaches -24 and +24 dB, so
they plateau at the clamp instead of continuing to rise. A raw bilinear design
avoids the near-Nyquist overshoot of prewarped corners; the cascade is
normalised at the pivot, swept for peak gain, and runs in double precision with
tiny states flushed to prevent denormals. The bright curve is this actual IIR
response, so its rounded knee is audible as drawn; deep cuts at extreme pivots
stay safe. FFT mode uses a 4096-point sqrt-Hann STFT with a hop of 512, applying
a real, smoothed gain to each bin and rebuilding the conjugate half before the
inverse transform. DC and Nyquist are treated separately. An aligned delay ring
lets the IIR and dry paths crossfade with the FFT path, and control changes
settle over about 50 ms.

### Wiring

- **Audio:** 2 in, 2 out, linked. Non-finite samples are treated as silence by
  the analyser and meters.
- **MIDI:** none. There is no CC map and no transport dependence.
- **Latency:** 0 samples in IIR mode; 4096 samples (85.3 ms at 48 kHz) in FFT
  mode, reported to the host. Bypass does not change the reported mode latency.
- **State:** none. All seven writable settings are ordinary host parameters;
  latency, two peaks and 64 spectrum bars are output-only.

## Status

Core tests pass (112,737 checks, including exact FFT unity round trip, measured
IIR and FFT slopes, the +24 dB ceiling in both the response plot and rendered
audio, linked stereo, bypass, output gain and block-size invariance), the VST3
target builds, and the reviewed 920x700 screenshot is published in the catalog.
Not yet listened to or validated in a host. See `plugins/tilt/docs/design.md`.
