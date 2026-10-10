---
title: Damiano
order: 36
bundle: damiano.vst3
kind: Audio effect
category: processor
role: Stereo distortion
screenshot: /assets/plugins/damiano.png
summary: Six-mode stereo distortion with drive, tone shelf, wet/dry mix and output gain. Linked or Split left/right processing for a binaural effect, with Drift CC control of each channel's drive and shape.
---

## Opinion

Covers the range from light warmth to aggressive fuzz. The Wavefold mode in particular produces sounds nothing else in the collection does. The MIDI CC drive link makes it a natural partner to Drift.

## Functionality

Damiano is a stereo distortion with six selectable waveshaping modes. Each mode shares the same Drive, Tone, Mix, and Output Gain controls; the Folds control is only active in Wavefold mode.

### Modes

| Mode      | Character                                                                 |
|-----------|---------------------------------------------------------------------------|
| Soft      | Polynomial soft-clip — gentle rounding, clean at low drive               |
| Tanh      | Hyperbolic-tangent waveshaper with output normalisation                   |
| Fuzz      | Asymptotic fuzz — positive and negative halves shaped differently for even harmonics |
| Overdrive | Biased tanh with DC cancellation, Tube-Screamer-style even-harmonic character |
| Tube      | Asymmetric: tanh on the positive half, x/√(1+x²) on the negative half    |
| Wavefold  | Triangle-wave folder applied in series for dense harmonic folding         |

### Parameters

| Parameter   | Range          | Default | Notes                                                              |
|-------------|----------------|---------|---------------------------------------------------------------------|
| Drive       | 1–10           | 2       | Waveshaping gain; behaviour varies by mode                         |
| Tone        | 0–100 %        | 50 %    | High-shelf: below 50 % darkens, above 50 % brightens              |
| Folds       | 1–8            | 2       | Cascade depth in Wavefold mode; greyed out in other modes          |
| Mix         | 0–100 %        | 100 %   | Wet/dry blend                                                      |
| Output Gain | −24 to +24 dB  | 0 dB    | Post-distortion level trim                                         |
| CC Drive    | off / 1–127    | off     | MIDI CC number that overrides Drive in real time (left, or both when Linked) |
| CC Shape    | off / 1–127    | off     | MIDI CC number that selects the Mode in six steps (left, or both when Linked) |
| CC Channel  | 1–16           | 1       | MIDI channel to listen on (shared)                                 |
| Stereo      | Linked / Split | Linked  | Split enables the right-channel settings below                     |
| Right Mode, Drive, Tone, Folds | as left | Tanh, 2, 50 %, 2 | Used only in Split                         |
| CC Drive R, CC Shape R | off / 1–127 | off | Right-channel CC numbers (Split only)                         |

### Tone shelf

The Tone control is a one-pole high-shelf filter applied before the distortion stage. At 50 % it is transparent. Below 50 % it attenuates high frequencies going into the distortion, producing a darker, warmer result. Above 50 % it boosts high frequencies before distortion, producing a brighter, harder edge. Crossover is fixed at approximately 3 kHz.

### MIDI CC drive

When CC Number is set to anything other than off, incoming MIDI CC messages on the selected channel and number override the Drive parameter for as long as a CC value is being received. CC value 0 maps to Drive 1, CC 127 maps to Drive 10. The override is released automatically when the transport stops; at that point the Drive slider resumes control.

This is designed to work directly with the Drift MIDI modulator. Route Drift's MIDI output to Damiano's MIDI input and assign the same CC number in both.

### Stereo: Linked and Split

The **Stereo** switch chooses how the two channels are processed.

- **Linked** (default): the right channel mirrors the left settings. This is the behaviour of earlier versions, and old sessions load as Linked.
- **Split**: the right channel has its own Mode, Drive, Tone and Folds, so each ear can be distorted differently. Fed the same signal on both sides, a Split setting such as Soft on the left and Fuzz on the right widens and detunes the image. Mix, Output Gain and the CC channel stay shared.

Each channel has its own **CC Drive** and **CC Shape** numbers (off / 1-127), all read on the shared CC Channel. CC Shape selects the waveshaper mode: the 0-127 range is divided into six equal steps (Soft, Tanh, Fuzz, Overdrive, Tube, Wavefold). In Linked mode the left CC numbers drive both channels and the right ones are ignored. Point two Drift lanes at the two CC Drive numbers to modulate the channels independently; a slow lane on one and a fast lane on the other gives a moving binaural beat.

### Wavefold notes

The triangle wavefolder applies the fold transfer function in series, re-driving the signal at each stage. High Drive combined with many Folds produces dense harmonic content that can sound noise-like at extreme settings — this is the intended character of the mode rather than an artefact. For cleaner folding tones, keep Drive below 5 and Folds at 2–4.

### Status

All six distortion modes, the MIDI CC drive link and the Linked/Split stereo processing are functional and covered by core tests (channel independence, CC mapping, state round-trip, pre-split state loading). The screenshot shows Split mode. Not yet auditioned in a host.
