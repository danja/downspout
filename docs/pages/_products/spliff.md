---
title: Spliff
order: 202
bundle: spliff.vst3
kind: Audio effect
role: Adaptive transient processor
screenshot: /assets/plugins/spliff.png
capabilities: [audio input and output, MIDI CC input]
summary: Transient processing in the Spiff manner — dynamic cuts or boosts that apply only where transient energy lives, steered by a 3-band detector, with Drift CC control.
---

## Opinion

Spliff earns its place on two jobs nothing else in the set does: de-clicking
vocal takes without touching the tone (Cut, high Sensitivity, short Decay) and
waking up flat drum overheads (Boost before the compressor). The Delta monitor
is the way in — if you cannot hear what it removes, you have the wrong
Sensitivity.

## Functionality

Spliff detects transients per frequency band and applies dynamic gain only
where transient energy exists. Mode selects Cut or Boost; Depth sets the
amount; Sensitivity the detection threshold; Sharpness the per-band
independence; Decay the recovery time with an LF/HF tilt; Split Low/High steer
the detector (sidechain weighting, not EQ). Mix, Trim, Delta and Bypass follow
the Spiff manual's workflow. The detector is mono (fixed full link), so
transients never shift the stereo image.

### Parameters

| Parameter    | Range          | Default       | Notes                                                      |
|--------------|----------------|---------------|------------------------------------------------------------|
| Mode         | Cut, Boost     | Cut           | Cut tames, Boost lifts                                     |
| Depth        | 0–100 %        | 50 %          | Amount of cut/boost; CC 1                                  |
| Sensitivity  | 0–100 %        | 50 %          | Higher catches more transients; CC 2                       |
| Sharpness    | 0–100 %        | 30 %          | Per-band independence; low = natural                       |
| Decay        | 0–100 %        | 25 %          | Recovery; 0 = onset only; CC 3                             |
| Decay LF/HF  | LF … HF        | equal         | Which end of the spectrum decays longer                    |
| Split Low    | 20–2000 Hz     | 250 Hz        | Detector low/mid crossover                                 |
| Split High   | 500–12000 Hz   | 4 kHz         | Detector mid/high crossover                                |
| Mix          | 0–100 %        | 100 %         | Dry/wet blend; CC 4                                        |
| Trim         | −12 to +12 dB  | 0 dB          | Wet-only makeup gain                                       |
| Bypass       | active/bypass  | active        | DSP keeps running                                          |
| Delta        | off/on         | off           | Monitor wet−dry                                            |
| CC Channel   | 1–16           | 1             | MIDI channel for all four CC overrides                     |

### Drift routing

Route Drift's MIDI output to Spliff's MIDI input. With default settings, all
four Drift lanes map directly: CC 1 → Depth, CC 2 → Sensitivity, CC 3 →
Decay, CC 4 → Mix. CC 0 disables the override for that parameter.

### Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER is pending. M/S operation,
parametric sensitivity bands, and a live reduction display are deferred.
