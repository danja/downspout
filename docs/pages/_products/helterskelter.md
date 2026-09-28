---
title: HelterSkelter
order: 203
bundle: helterskelter.vst3
kind: Audio effect
category: processor
role: Automatic wah pedal
screenshot: /assets/plugins/helterskelter.png
capabilities: [audio input and output, MIDI CC input, host transport]
summary: Automatic wah — a resonant lowpass played by the input envelope and/or a BBT-synced ADSR cycle, with Drift CC control.
---

## Opinion

HelterSkelter is at its best doing the two jobs a static wah cannot: touch-wah
that tracks picking dynamics on funk guitar, and tempo wah that sweeps a pad
or clav part in exact bars. Start in Blend with Depth near full — picked
accents punch through the cycle, which is the whole point of the max
combination. If the sweep feels backwards against the groove, Invert is the
one-button fix, not the ADSR.

## Functionality

A resonant lowpass (Q 0.5–12) whose cutoff is the sum of intent: the input
envelope opens it like a touch-wah from Base Freq up to four octaves above
(Depth), while a BBT ADSR retriggers every Division cycle (1, 2, 4 or 8 beats)
with Gate Beats high and releases between. Blend takes whichever opens
further. Invert flips the gate for low-then-high patterns; with the transport
stopped the ADSR releases and the envelope path carries on alone.

### Parameters

| Parameter    | Range          | Default       | Notes                                                      |
|--------------|----------------|---------------|------------------------------------------------------------|
| Mode         | Env, BBT, Blend| Env           | Cutoff source                                              |
| Sensitivity  | 0–100 %        | 60 %          | Envelope drive; CC 1                                       |
| Depth        | 0–100 %        | 70 %          | Sweep range up to +4 octaves; CC 2                         |
| Resonance    | Q 0.5–12       | 4             | Filter peak; CC 3                                          |
| Base Freq    | 100–2000 Hz    | 400 Hz        | Pedal-down cutoff                                          |
| Division     | 1/2/4/8 beats  | 4 beats       | ADSR cycle length                                          |
| Gate Beats   | 0.5–8          | 2             | High beats per cycle, clamped to Division                  |
| Attack       | 1–500 ms       | 20 ms         | ADSR attack                                                |
| Decay        | 5–1000 ms      | 150 ms        | ADSR decay to Sustain                                      |
| Sustain      | 0–100 %        | 70 %          | ADSR sustain level                                         |
| Release      | 5–2000 ms      | 200 ms        | ADSR release                                               |
| Invert       | off/on         | off           | Flip the BBT gate                                          |
| Mix          | 0–100 %        | 100 %         | Dry/wet blend; CC 4                                        |
| Trim         | −12 to +12 dB  | 0 dB          | Wet-only makeup gain                                       |
| Bypass       | active/bypass  | active        | DSP keeps running                                          |
| CC Channel   | 1–16           | 1             | MIDI channel for all four CC overrides                     |

### Drift routing

Route Drift's MIDI output to the wah's MIDI input. With default settings, all
four Drift lanes map directly: CC 1 → Sensitivity, CC 2 → Depth, CC 3 →
Resonance, CC 4 → Mix. CC 0 disables the override for that parameter.

### Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER is pending.
