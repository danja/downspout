---
title: Silken
order: 240
bundle: silken.vst3
kind: Audio effect
category: processor
group: dynamics
role: Vocal strip with de-esser
screenshot: /assets/plugins/silken.png
capabilities: [audio input, audio output]
summary: A vocal clean-up and polish strip with a rumble filter, plosive guard, de-esser (band or wide, with listen), warmth, presence and air EQ, sparkle and output level.
---

## Opinion

Not yet auditioned on a real voice. The defaults are protective rather than coloured: an 80 Hz rumble filter, a plosive guard at -20 dB and a de-esser at 6.5 kHz
that only wakes up when the sibilance is well above the rest of the voice, with the EQ and sparkle at zero. Put it first on a vocal, press **Listen** and sweep the
de-esser **Frequency** until you hear the s sounds on their own, then pull the **Threshold** down until the meter just moves on them.

## Functionality

In order: **Rumble Filter** (high-pass), **Plosive Guard** (a detector on the band below 150 Hz that fades the signal towards a high-pass when a breath burst arrives),
**De-esser** (a two-stage band-pass detector, linked across both channels; **Band** cuts a bell at the frequency, **Wide** turns the whole signal down; **Range**
limits it; **Listen** plays the detector), then **Warmth** (low shelf), **Presence** (bell at 3.2 kHz), **Air** (high shelf at 10 kHz), **Sparkle** (adds harmonics
of the top end) and **Output**. Every stage is exactly neutral when it is off, and the panel's meters show the sibilance level against the threshold and the gain reduction.
No latency.

### Status

Core covered by tests (195 checks) against formulas worked out by hand: the rumble filter curve, the plosive and de-esser reductions, the range cap, vowel rejection,
Band against Wide, the EQ gains, sparkle, block-size independence and idle decay. The times, ratios and corner frequencies are by ear and have not been tried on real vocals or
in a host.
