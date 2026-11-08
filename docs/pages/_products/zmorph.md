---
title: Zmorph
order: 243
bundle: zmorph.vst3
kind: Audio effect
category: processor
group: tone
role: Z-plane morph filter
screenshot: /assets/plugins/zmorph.png
capabilities: [audio input, audio output]
summary: One resonant filter that morphs continuously between a low-pass and a high-pass at the same cutoff, so half-way is a deep notch. Cutoff, resonance, morph, mix and output level.
---

## Opinion

A single knob from low-pass to high-pass through every shape between them. Park **Morph**
at 0 and it is an ordinary resonant low-pass; at 100 it is the same filter inverted into a
high-pass; at 50 the two numerators cancel at the cutoff and it becomes a narrow notch at
exactly the frequency the knob is pointing at. Sweeping Morph therefore moves a bump from
below the cutoff to above it, or opens and closes a notch, without the cutoff moving at all.

That is a different gesture from a normal multimode filter, where the shape is a switch and
the cutoff does the travelling. Here **Cutoff** sets the frequency the shape is built
around, **Resonance** sets how sharp it is, and **Morph** decides which side of that
frequency is kept. It is a good candidate for a Drift lane or an automation envelope on
sources where you want the weight to slide between the body and the air.

Because the two shapes share one denominator, the transition is smooth in the coefficients
— but the output level is not constant across it (roughly -6 dB through the middle), so
expect to ride **Level** or automate it if the sweep has to stay even.

## Functionality

A biquad per channel. The low-pass and high-pass numerator coefficients for
**Cutoff** and **Resonance** are computed each block and mixed linearly by **Morph**; the
denominator (the two poles) is shared, so morphing moves the zero without moving the
resonance. At Morph 50 % the blended numerator is 0.5 - cos(w0) z⁻¹ + 0.5 z⁻², which has a
zero exactly at the cutoff, hence the notch.

**Resonance** maps to Q = 0.5 / (1 - Resonance), so 50 % is Q ≈ 1, 90 % is Q ≈ 5 and 99 %
is Q ≈ 50; the biquad is the standard bilinear-transform form, stable across the range at
44.1-192 kHz. **Mix** is dry against filtered (bit-exact dry at 0), **Level** is an output
trim. Coefficients are recomputed once per block, so automating Cutoff moves in steps.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Cutoff | 20 Hz-20 kHz | 1 kHz | Clamped below Nyquist |
| Resonance | 0-99 % | 50 | Q = 0.5 / (1 - Resonance) |
| Morph | 0-100 % | 50 | 0 = low-pass, 50 = notch, 100 = high-pass |
| Mix | 0-100 % | 100 | |
| Level | -24 to +12 dB | 0 | |

### Origin

Its own design. The name refers to the pole/zero picture of the biquad rather than to any
particular hardware; it is a coefficient morph between two numerators, not a sampled
Z-plane filter.

### Status

Core DSP, the VST3 target and the catalogue screenshot are complete. The response has been
measured (low-pass and high-pass reach 0 dB at their passband ends, the notch at Morph 50 %
measures -109 dB at the cutoff at mid resonance, -75 dB at 99 %), but the one-assertion core
test only checks finiteness, and nothing has been auditioned or run in a host.
