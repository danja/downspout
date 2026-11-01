---
title: Mouth
order: 235
bundle: mouth.vst3
kind: Audio effect
category: processor
role: Vowel filter
screenshot: /assets/plugins/mouth.png
capabilities: [audio input, audio output, MIDI CC input]
summary: Puts a vowel on any sound. Five formant filters morph between A, E, I, O and U on an x-y pad, for bass, tenor, alto and soprano voices, with shift, resonance, stereo spread, drive and air.
---

## Opinion

A talking filter. Feed it a saw pad, a drum loop or noise and drag the pad: the sound moves
through "ah", "ee", "oo" without any speech in the source. It is the classic formant-filter
move (think talking synth lines and "wow"-ing basses) with the vowels laid out as a map rather
than a knob, so a gesture, a Drift CC or an automation lane can wander between them.

It works best on material with plenty of harmonics and sustain. A dull or very low source has
little energy at the upper formants, so raise **Drive** or **Air**, or shift the voice down.
**Resonance** is the character control: low is a soft vowel colour, high is a narrow,
pitched, nasal ringing. **Spread** detunes the upper formants in opposite directions on the two
sides for a wide, slightly unreal choir.

It does not speak: it colours what you give it. For a vocoder see voxmod; for moving the
formants of an existing voice see quefrency.

## Functionality

Five parallel band-pass filters (zero-delay-feedback state-variable filters, stable under fast
modulation) sit at the formant frequencies of the chosen vowel; their outputs are summed with
the tabulated level of each formant. The pad blends the five vowel tables by inverse-distance
weighting around the vowel positions (frequency and bandwidth in the log domain, level in dB),
so every pad position is a smooth blend and each vowel is exact where it sits. The vowel
chart is laid out like a phonetics chart: down is how open the mouth is (first formant),
across is tongue front to back (second formant).

**Shift** moves every formant by up to an octave either way (a larger or smaller vocal tract),
**Resonance** scales the bandwidths (50 % = as tabulated, 0 % four times wider, 100 % four times
narrower), **Spread** detunes formants 2 to 5 in opposite directions on the left and right,
**Drive** saturates before the filters (a small signal gains half the setting in dB),
**Air** blends the unfiltered highs (above about 4 kHz) back in, **Mix** is dry against wet
(0 % is the input bit for bit) and **Output** trims the wet level. The wet level is normalised
so white noise comes out near its input level for every vowel (within 2.5 dB, measured).

Controls that shape the filters are smoothed over 20 ms. Drift CC 1-4 on the **CC Channel**
move Vowel X, Vowel Y, Shift and Resonance; moving the control on the panel or from the host
takes it back. No latency, no plugin state.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Vowel X | 0-100 % | 50 | Front (I, E) to back (U, O); Drift CC 1 |
| Vowel Y | 0-100 % | 95 | Closed (I, U) to open (A); Drift CC 2 |
| Voice | Bass, Tenor, Alto, Soprano | Tenor | Formant table |
| Shift | -12 to +12 st | 0 | Drift CC 3 |
| Resonance | 0-100 % | 50 | Drift CC 4 |
| Spread | 0-100 % | 0 | |
| Drive | 0-24 dB | 0 (off) | |
| Air | 0-100 % | 0 | |
| Mix | 0-100 % | 100 | |
| Output | -24 to +12 dB | 0 | |
| CC Channel | off, 1-16 | 1 | |

### Origin

Formant frequencies, levels and bandwidths are the widely circulated Csound formant table
(five formants, bass, tenor, alto and soprano). The pad layout and blending are Downspout's own.

### Status

Core DSP, about 1 700 deterministic checks, the VST3 target and the catalogue screenshot are
complete. It has not been auditioned and has not been run in a host. Changing **Voice** switches
the formant table at once, so it can click on a loud sustained source.
