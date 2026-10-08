---
title: Pratt
order: 207
bundle: pratt.vst3
kind: Instrument
category: instrument
role: Number-theoretic synth and filter
screenshot: /assets/plugins/pratt.png
capabilities: [audio input, audio output, MIDI note and CC input]
summary: Eleven General MIDI voices and a stable all-pole filter, all shaped by the factorisation of an integer index — play it, filter audio with it, or both.
---

> **Based on [pratt-synth](https://github.com/githubuser1983/pratt-synth)** — the original Pratt-polynomial MIDI
> synthesizer and sonification code (Python, offline renderer, with the source
> paper and example recordings). Pratt is a real-time port of that work; the
> voice spectra match the original renderer to within 0.12 dB.

## Opinion

An odd and likeable instrument: neighbouring keys have unrelated spectra because
their indices factor differently, so a scale sounds slightly "alive" in a way
ordinary filtered oscillators do not. The filter is gentler than it looks on
paper — low indices are smooth low-passes, and the interesting territory is in
the composites, where cascading Index A and Index B stacks poles in
pairs that only the number theory predicts. Treat the drums as a bonus, not a
kit.

## Functionality

Pratt is a real-time port of the offline `pratt-synth` renderer. Everything comes
from the Pratt polynomials: `f_1 = 1`, `f_2 = x`, `f_p = 1 + f_{p-1}` for odd
primes, multiplicative otherwise, with `f_n(2) = n`. The filter

    H_n(s) = n / f_n(2 + s/w0)

is all-pole, has unity gain at DC and is strictly stable (poles at least 0.99 w0
into the left half plane for every n up to 8192). Because `H_m H_n = H_mn`,
two index controls cascade into one filter of index `A x B`.

**Synth mode.** Each note gets index `n = (note + 1) x base`; partial k of the
note is weighted by `k^-roll * H_n(i xi k)` using both magnitude and phase, then
summed into a band-limited table. Eleven families follow the General MIDI
program (or can be fixed): piano, electric piano, organ, pluck, bass, strings,
brass, reed, flute, pad and timpani. Sustain pedal, channel volume, expression,
pan and pitch bend are handled; channel 10 plays procedural drums (a swept-sine
kick, the rest noise through `H_(note+1)`). A fixed 14-tap diffuse room and a
soft clip finish the voice path.

**Timbre Index.** A large Pratt base darkens a voice but also washes out the
key-to-key variation, because the same factors are multiplied into every note.
Timbre Index instead cascades one fixed filter `H_timbre` onto every note
(`H_mn = H_m H_n`), so the character from `(note + 1) x base` stays and the
chain only adds roll-off. It defaults to 1, which adds nothing; it is applied when
a wavetable is built, so it costs no extra time per sample at any setting.

**Filter mode.** Audio in goes through the Pratt filter at the chosen cutoff
with a dry/wet mix. The panel plots the magnitude response and the poles.
**Synth + Filter** sums the voices and the audio input before the filter.

The voice wavetables are built on a background thread after the plugin is
activated, so the first chord does not pay for them.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Mode | Synth, Filter, Synth + Filter | Synth | Signal flow is shown on the panel |
| Voice | GM Program + 11 families | GM Program | Fixed voice overrides program changes |
| Pratt Base | 0–64 | 0 | 0 uses the voice's own base; `n = (note + 1) x base` |
| Timbre Index | 1–8192 | 1 (off) | Extra fixed filter chain `H_timbre` on every note; darker, key-to-key character kept |
| Brightness | 0.25–3.0 | 1.0 | More partials pass |
| Rolloff | −0.5 to 1.5 | 0 | Positive is darker |
| Bend Range | 0–12 st | 2 | RPN is not parsed |
| Drums Ch10 | Off, On | On | Procedural percussion |
| Room | 0–100 % | 100 % | Fixed 14-tap send |
| Level | 0–200 % | 50 % | Ahead of a soft clip |
| Index A / Index B | 1–128 / 1–64 | 5 / 7 | Effective index A × B, capped at 8192 |
| Cutoff | 20–4000 Hz | 800 Hz | Clamped to 0.1 × sample rate |
| Filter Mix | 0–100 % | 100 % | Dry/wet |

### Status

Core DSP, voice parity with the Python renderer (within 0.12 dB), deterministic
core, engine, state and threading tests (the last also clean under
ThreadSanitizer), the VST3 target and the catalogue screenshot are complete. Drum
levels have not been tuned by ear, and host validation in REAPER remains.
