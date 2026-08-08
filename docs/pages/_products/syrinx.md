---
title: Syrinx
order: 83
bundle: syrinx.vst3
kind: Instrument
role: Polyphonic birdsong synthesizer
screenshot: /assets/plugins/syrinx.png
summary: Polyphonic MIDI synthesizer based on the Mindlin-Laje biophysical avian syrinx model, with 10 bird-species presets, chromatic MIDI pitch mapping, and a vertical-slider NanoVG UI.
---

## Functionality

Syrinx is a polyphonic synthesizer that models avian vocal production using the
Mindlin-Laje ordinary differential equation syrinx model — the same physics that
produces birdsong. Each voice runs an explicit Euler ODE at audio rate with
optional oversampling, a pitch-tracking bandpass vocal-tract filter, formant
bank, respiration model, and noise/roughness source modifiers.
The DSP was ported from the glorious [Lyrebird](https://github.com/sha5b/Lyrebird) project.

It responds chromatically to MIDI note input: note number maps to base
frequency (A4 = 440 Hz) and velocity scales amplitude. Up to eight voices play
simultaneously with oldest-completed voice stealing.

### Presets

Ten built-in presets cover a range of timbres and behaviours:

| # | Name | Character |
|---|------|-----------|
| 1 | Wren | Fast, complex, high-frequency warble |
| 2 | Thrush | Rich, melodic phrases with formant emphasis |
| 3 | Warbler | Sustained tones with slow vibrato |
| 4 | Finch | Short bright chips, high pitch |
| 5 | Robin | Mid-range, pure, rounded contour |
| 6 | Nightjar | Low raspy churn — noise and roughness |
| 7 | Pigeon | Soft coo, respiration-driven, low coupling |
| 8 | Hummingbird | Ultra-fast AM trill, high pitch |
| 9 | Starling | Buzzy reed timbre, irregular roughness |
| 10 | Custom | Blank default for user patches |

### Parameters

Per-preset controls (vertical sliders): Level, Noise, Roughness, Timbre,
Vibrato Rate, Vibrato Depth, Contour Bend, Harmonic, AM Rate.

Master controls: Distance (air absorption + reverb mix), Gain.

### MIDI

| CC | Parameter |
|----|-----------|
| 1  | Vibrato Depth |
| 7  | Master Volume |
| 71 | Roughness |
| 74 | Timbre |
| 76 | Vibrato Rate |
| 77 | Noise |
| 91 | Distance |
| 94 | Contour Bend |

Pitch bend spans ±2 semitones across all active voices.

### Status

First implementation. DSP core, MIDI handling, and NanoVG vertical-slider UI
are complete. Preset tuning and host validation are ongoing.
