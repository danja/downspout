---
title: Quefrency
order: 161
bundle: quefrency.vst3
kind: Audio effect
role: Cepstral formant and harmonic shifter
screenshot: /assets/plugins/quefrency.png
capabilities: [audio input, audio output, MIDI CC input, reported latency]
summary: Splits each frame through the cepstrum into a formant envelope and a harmonic structure, shifts and reshapes each independently, and multiplies them back together — with the latency the web version never reported now declared to the host.
---

## Opinion

The formant knob is the one: +7 semitones on a vocal-ish pad lifts the vowels
without touching the notes, which no plain pitch shifter can do. The price is
the usual phase-vocoder smear on transients and a two-thousand-sample delay
the host has to swallow — both honestly declared. Keep the lifter below the
shortest pitch period or the whole trick leaks.

## Functionality

Quefrency is a port of the JigDAW plugin of the same name (Rust/WebAssembly).
Each stereo frame (2048 samples at 50 kHz and below, 4096 above, hop a
quarter) is FFT'd; the log magnitude is cepstrally smoothed at the lifter
split into a formant envelope and a harmonic excitation; the envelope is
warped (shift), contrasted (depth) and tilted; excitation peaks move by pitch
ratio and Hz offset with Laroche–Dolson phase rotation; everything is applied
as a gain on the input spectrum, so neutral settings reconstruct the input
exactly, delayed by N−1. The dry path carries the same delay, so half mix
does not comb.

Latency is 2047 samples at 50 kHz and below, 4095 above, and is reported to
the host — the web module ABI carries no latency field, which is the timing
problem this port fixes. MIDI CC 70–80 drive the eleven parameters in order
on any channel with centre-detent mapping; a CC holds until the host moves
that control.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Formant shift | −12…+12 st | 0 | Envelope axis warp. CC 70 |
| Formant depth | 0–200 % | 100 % | 0 flattens, 200 exaggerates. CC 71 |
| Formant tilt | −6…+6 dB/oct | 0 | About 1 kHz. CC 72 |
| Pitch shift | −24…+24 st | 0 | Peak ratio. CC 73 |
| Pitch fine | −100…+100 ct | 0 | Peak ratio trim. CC 74 |
| Freq shift | −1000…+1000 Hz | 0 | Inharmonic offset. CC 75 |
| Harmonic depth | 0–200 % | 100 % | 0 breathy, 200 buzzy. CC 76 |
| Lifter | 0.5–5 ms | 1.5 ms | Cepstral split point. CC 77 |
| Estimator | Cepstral, True envelope | Cepstral | CC 78 |
| Mix | 0–100 % | 100 % | Dry delayed to match. CC 79 |
| Output | −24…+12 dB | 0 dB | CC 80 |

### Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER remains.
