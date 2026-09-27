---
title: Primefold
order: 160
bundle: primefold.vst3
kind: Audio effect
role: Prime-harmonic feedback shifter
screenshot: /assets/plugins/primefold.png
capabilities: [audio input, audio output, MIDI CC input]
summary: Direct 2x/3x/5x pitch voices with non-prime harmonics supplied by recirculation — 4x from 2x fed back, 6x from 2x and 3x combined — through a bounded loop with an explicit delay that is never compensated.
---

## Opinion

The ladder diagram is the whole instrument: set 2x/3x/5x, then push Feedback
until 4x and 6x light up behind them. It reads as one resonances stack rather
than three detuned shifters, which is exactly the point — composites arrive
late and a little darker, so chords bloom upward instead of just getting
louder.

## Functionality

Primefold pitch-shifts stereo input directly to the prime ratios 2x, 3x and
5x with time-domain dual-tap resampling shifters sharing one grain window
(512/1024/2048 samples). The three voices recirculate all-to-all through an
explicit 256-sample delay and a per-pass damping low-pass: 4x is 2x fed back
into 2x, 6x is 2x into 3x, 8x three passes through 2x, 9x two passes through
3x, 10x 2x/5x, and so on up the ladder.

The loop is contractive by construction (passive shifters, 1/sqrt(3) fan-in,
feedback capped at 0.90, tanh plus DC discipline, isfinite panic guard).
Feedforward latency equals the grain window, is reported to the host, and is
matched on the dry path; in-loop latency is never compensated, so composites
bloom late by `k x (grain + 256)` per pass.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Dry | 0–100 % | 60 % | Delayed by one grain to align with voices |
| 2x / 3x / 5x | 0–100 % | 70 / 50 / 40 % | Direct prime voices |
| Feedback | 0–90 % | 35 % | Recirculation gain. CC 1 |
| Damp | 0–100 % | 50 % | Per-pass loop low-pass |
| Grain | 512 / 1024 / 2048 | 1024 | Shifter window; reported latency |
| Mix | 0–100 % | 60 % | Dry/wet. CC 2 |
| Width, Output | 0–100 % | 60 / 75 % | Mid/side spread, level. CC 7 |

### Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER remains.
