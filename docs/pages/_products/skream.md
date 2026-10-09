---
title: Skream
order: 37
bundle: skream.vst3
kind: Audio effect
category: processor
role: Scream filter
screenshot: /assets/plugins/skream.png
summary: SVF lowpass/highpass with ADAA2 feedback saturation. Recreates the Skrillex-era dubstep growl. Ten presets from classic growl to feedback drone; Cutoff and Scream modulatable via MIDI CC from Drift.
---

## Opinion

The feedback topology is what makes this distinct — the highpass-filtered, saturated feedback creates formant-like mid-range aggression that changes character with Resonance far more dramatically than a straightforward overdrive. At max Resonance with a low Cutoff it self-threatens to self-oscillate (the expander gate prevents blowup); at low Resonance it is a simple pre-LP saturator. The ten presets cover the parameter space well enough that most heavy electronic bass patches start from one of them.

## Functionality

Skream implements the signal topology of a scream filter: input passes through an ADAA2 anti-aliased tanh saturator and a State Variable lowpass filter, while a scaled and highpass-filtered copy feeds back through a second saturator into the input. An envelope-follower expander gate mutes the feedback automatically when the input is silent.

### Signal chain

```
Input → Input Gain → [+ fbYn1] → tanh₁ → LP SVF → wet mix → Output Gain → Out
                                                ↓
                                        fbYn1 ← expander ← tanh₂ ← HP SVF ← ×feedback
```

### Parameters

| Parameter   | Range         | Default | Notes                                                          |
|-------------|---------------|---------|----------------------------------------------------------------|
| Input Gain  | −24 to +24 dB | 0 dB    | Pre-saturation level                                           |
| Cutoff      | 0–100 %       | 85 %    | LP filter frequency (log: 0 % ≈ 20 Hz, 100 % ≈ 20 kHz)       |
| Scream      | 0–100 %       | 46.5 %  | HP feedback cutoff (same mapping, clamped ≤ Cutoff)           |
| Resonance   | 0–100 %       | 100 %   | Controls HP filter Q and feedback gain (−12 to +12 dB)        |
| Mix         | 0–100 %       | 100 %   | Wet/dry blend                                                  |
| Output Gain | −24 to 0 dB   | −6 dB   | Post-filter output level                                       |
| Track       | 0–100 %       | 0 %     | Injects a fraction of the input into the feedback path (harmonic locking) |
| Morph LP-HP | −100 to +100 % | 0 % (off) | Filter shape follows the cutoff: + is low-pass at a low cutoff and morphs towards high-pass as it rises; − is the mirror image. 0 is the plain low-pass |
| CC Cutoff   | off / 1–127   | off     | MIDI CC number to override Cutoff in real time                 |
| CC Scream   | off / 1–127   | off     | MIDI CC number to override Scream in real time                 |
| CC Channel  | 1–16          | 1       | MIDI channel for CC overrides                                  |

### Presets

| # | Name              | Character                                          |
|---|-------------------|----------------------------------------------------|
| 1 | Classic Growl     | Scream plugin defaults — balanced dubstep baseline |
| 2 | Wobbly Bass       | Low cutoff, high resonance                         |
| 3 | Scream Lead       | High scream/HP, mid cutoff                         |
| 4 | Metallic Edge     | Very high cutoff and scream, moderate resonance    |
| 5 | Vowel Formant     | Mid cutoff, maximum resonance — vowel character    |
| 6 | Tight Bite        | High input gain, lower resonance — punchy          |
| 7 | Squelch           | Near-max cutoff and scream                         |
| 8 | Dubstep Classic   | Canonical brostep balance                          |
| 9 | Subtle Saturation | Gentle overdrive, low scream, partial wet          |
|10 | Feedback Drone    | Max resonance — near self-oscillation              |

### Morph: LP to HP across the cutoff

The forward filter is a state-variable filter, so low-pass and high-pass outputs exist at once. **Morph** blends them by an amount that depends on where the cutoff sits: the weight on the high-pass is `|Morph| x cutoff position` for positive Morph (0 at the bottom of the cutoff range, rising with it) and `|Morph| x (1 - cutoff position)` for negative. At weight 0 the filter is exactly the old low-pass, at 0.5 it is a notch at the cutoff, and at 1 it is a high-pass. At +100 %, sweeping Cutoff up therefore turns a low-pass growl into a high-pass bite; at −100 % the sweep runs the other way. It follows the *effective* cutoff, so a Drift or CC lane on Cutoff sweeps the shape as well as the frequency. Morph 0 (the default, and in every preset) leaves the output unchanged. The Scream feedback high-pass is not affected.

### MIDI CC control

Set CC Cutoff and/or CC Scream to a CC number (1–127) and the corresponding parameter responds to MIDI CC in real time without losing the slider position. Route Drift's output to Skream's MIDI input and set matching CC numbers in both for synchronised modulation.

### DSP notes

The saturator uses second-order ADAA (anti-derivative anti-aliasing) applied to a tanh nonlinearity, requiring double-precision arithmetic internally. This suppresses aliasing at the cost of higher per-sample CPU relative to a plain `std::tanh`. The SVF uses the Cytomic linear-trap topology with a fixed Butterworth Q for the lowpass and a resonance-dependent Q for the highpass.

### Status

Core DSP, preset system, and MIDI CC modulation are functional and tested. Core DSP, preset system, MIDI CC modulation and the Morph LP-HP control are functional and tested; the screenshot shows the current panel.
