---
title: Spliff
order: 202
bundle: spliff.vst3
kind: Audio effect
category: processor
group: dynamics
role: Adaptive transient processor
screenshot: /assets/plugins/spliff.png
capabilities: [audio input and output, MIDI CC input]
summary: Transient processing with 3-band Spiff-style Cut/Boost plus a level-independent, threshold-free Attack/Sustain Designer, with Drift CC control.
---

## Opinion

Spliff earns its place on two jobs nothing else in the set does: de-clicking
vocal takes without touching the tone (Cut, high Sensitivity, short Decay) and
waking up flat drum overheads (Boost before the compressor). Its new Designer
mode is the blunt, level-independent tool: two envelopes and no threshold, so
the same Attack/Sustain setting behaves the same on a whisper and a loud kit.
The Delta monitor is the way in — if you cannot hear what it removes, you have
the wrong Sensitivity.

## Functionality

In **Cut/Boost**, Spliff detects transients per frequency band and applies
dynamic gain only where transient energy exists. Depth sets the amount;
Sensitivity the detection threshold; Sharpness the per-band independence; Decay
the recovery time with an LF/HF tilt; Split Low/High steer the detector
(sidechain weighting, not EQ).

In **Designer**, a shared fast/slow envelope pair replaces the three-band
detector. Their dB ratio drives Attack (±15 dB, onsets where fast > slow) or
Sustain (±24 dB, body/tails where slow > fast) with no threshold. Because the
decision is a ratio, changing input level does not change the shaping. Both
controls default to 0 dB, so existing projects are untouched.

Mix, Trim, Delta and Bypass apply in every mode. The detector is mono (fixed
full link), so gain changes never shift the stereo image.

### Parameters

| Parameter    | Range          | Default       | Notes                                                      |
|--------------|----------------|---------------|------------------------------------------------------------|
| Mode         | Cut, Boost, Designer | Cut     | Designer swaps the rows for Attack/Sustain                |
| Depth        | 0–100 %        | 50 %          | Amount of cut/boost; CC 1; Cut/Boost only                  |
| Sensitivity  | 0–100 %        | 50 %          | Higher catches more transients; CC 2; Cut/Boost only       |
| Sharpness    | 0–100 %        | 30 %          | Per-band independence; low = natural; Cut/Boost only       |
| Decay        | 0–100 %        | 25 %          | Recovery; 0 = onset only; CC 3; Cut/Boost only             |
| Decay LF/HF  | LF … HF        | equal         | Which end of the spectrum decays longer                    |
| Split Low    | 20–2000 Hz     | 250 Hz        | Detector low/mid crossover                                 |
| Split High   | 500–12000 Hz   | 4 kHz         | Detector mid/high crossover                                |
| Mix          | 0–100 %        | 100 %         | Dry/wet blend; CC 4                                        |
| Trim         | −12 to +12 dB  | 0 dB          | Wet-only makeup gain                                       |
| Bypass       | active/bypass  | active        | DSP keeps running                                          |
| Delta        | off/on         | off           | Monitor wet−dry                                            |
| Attack       | −15 to +15 dB  | 0 dB          | Designer only; fast envelope (onsets)                      |
| Sustain      | −24 to +24 dB  | 0 dB          | Designer only; slow envelope (body/tails)                  |
| CC Channel   | 1–16           | 1             | MIDI channel for all four CC overrides                     |

### Drift routing

Route Drift's MIDI output to Spliff's MIDI input. With default settings, all
four Drift lanes map directly: CC 1 → Depth, CC 2 → Sensitivity, CC 3 →
Decay, CC 4 → Mix. CC 0 disables the override for that parameter. Designer
uses panel Attack/Sustain; only CC 4 Mix reaches that mode.

### Status

Core DSP (including Designer), deterministic tests, the VST3 target and the
panel are complete. The catalogue screenshot predates the three-way selector;
fresh Cut and Designer captures were taken but visual acceptance is pending.
Host validation in REAPER is pending. M/S operation, parametric sensitivity
bands, and a live reduction display are deferred.
