---
title: Sieve
order: 242
bundle: sieve.vst3
kind: Audio effect
category: processor
group: spectral
role: Spectral scale sieve
screenshot: /assets/plugins/sieve.png
capabilities: [audio input, audio output, MIDI note input, reported latency]
summary: An STFT mask that keeps only the partials near the notes of a chosen scale (or of the MIDI notes you hold), in every octave. The source keeps its own texture instead of being re-synthesised into new resonators.
---

## Opinion

A filter that thinks in notes rather than in frequencies. Point it at a scale and hold a
chord, and a noisy or inharmonic source (a cymbal, a breath, a guitar chord, a field
recording) keeps only the partials that belong to those notes — the rest is attenuated by
**Floor**. Because the phase and the shape of each surviving partial are the source's own,
the result sounds like the same material through a comb, not like a new instrument playing
it.

That is what separates it from `resonance-garden`, which rings its own resonators: Sieve
carves, it does not add. **Width** sets how much of the neighbourhood of each note survives
(narrow is a tuned comb, wide keeps the original character), and **Tilt** leans the odd and
even harmonics against each other so a note can be made hollow or reedy.

**Source** chooses what the mask is built from: **Scale** uses **Root** and **Scale** and
applies the same set of pitch classes in every octave, while **Held MIDI** uses the notes
currently held on **MIDI In**, so a chord on a controller moves the mask as you play. When
no note is held, Held mode falls back to **Root** and **Scale** rather than going silent.

## Functionality

A 1024-point short-time Fourier transform (square-root Hann analysis and synthesis windows,
256-sample hop) runs on each channel. Every bin is converted to a fractional MIDI note, the
nearest allowed note is found (a scale degree, or one of the held notes), and the bin is
given a Gaussian gain in cents centred on it — so a passband is a bump around each note
rather than a rectangle — floored at **Floor** dB. Odd and even bins are scaled in opposite
directions by up to 30 % each way for **Tilt**, and the masked spectrum is transformed back
and overlap-added.

The latency (1024 samples: 21 ms at 48 kHz) is reported to the host and the dry path is
delayed to match, so **Mix** blends in phase.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Root | C-B (12) | C | |
| Scale | 24 scales | Major | The canonical list in `docs/scales.md` |
| Source | Scale, Held MIDI | Scale | |
| MIDI In | off, 1-16 | off | Channel filter for Held MIDI |
| Width | 1-200 cents | 50 | Gaussian half-width around each note |
| Floor | -80-0 dB | -60 | Minimum gain: how much of the rest survives |
| Tilt | -1 to +1 | 0 | Odd against even harmonics, up to 30 % |
| Mix | 0-100 % | 100 | Dry is bit-exact at 0 |
| Level | -24 to +12 dB | 0 | Output trim |

### Origin

The technique is the classic sinusoidal "scale mask": an STFT, a per-bin gain derived from a
pitch-class set, and overlap-add. The implementation, the Gaussian passband in cents and the
odd/even tilt are Downspout's own.

### Status

Working. The wet path resynthesises correctly: a mask of unity in every bin is a pure delay of
1024 samples (white noise within 0.01 dB, correlation 1.000000), an impulse produces a single output
tap, and the result does not depend on the host block size. The core tests cover the round trip,
Width, Floor, Tilt, Mix, Level, Root/Scale, and channel-filtered Held MIDI.
