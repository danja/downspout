---
title: Syrinx
order: 83
bundle: syrinx.vst3
kind: Instrument
category: instrument
role: Birdsong synthesizer and voice-to-bird effect
screenshot: /assets/plugins/syrinx.png
summary: Avian syrinx voice based on the Mindlin-Laje biophysical model with 10 bird-species presets. MIDI notes play phrases of chirping syllables; an audio input is analysed for pitch and loudness and steers the bird, so it works on speech and singing.
---

## Opinion

Not bad. Birdish. It lacks the variety of the Lyre site/app on which it is based but is still interesting/useful.

## Functionality

Syrinx models avian vocal production with the Mindlin-Laje ordinary differential
equation syrinx model, the same physics that produces birdsong. Each voice runs an
explicit Euler ODE at audio rate with oversampling, a pitch-tracking bandpass vocal
tract, a tracheal formant comb, a respiration model and noise/roughness source
modifiers. The DSP was ported from the glorious
[Lyrebird](https://github.com/sha5b/Lyrebird) project.

The oscillator is **pitch-locked**: the ODE's frequency is gamma x Phi(alpha, beta) and
Phi moves with pressure, so the voice measures its own period every cycle and corrects
gamma to hold the target f0. A pitch contour therefore sweeps the sound itself, not only
the filter, which is what makes a chirp a chirp.

### Source

- **MIDI**: each note-on plays a *phrase* of one to sixteen short syllables. **Chirp**
  shortens the preset's duration toward a real syllable (x0.28 at 60 %) and sets the sweep
  depth (about 1.3 octaves at 60 %, 25-100 oct/s on the short presets). Every syllable gets
  its own contour (ramp up, ramp down, arch or valley, leaning the way the preset's Bend
  does), pitch, length, loudness and noise, spread by **Variation**. **Gap** sets the silence
  between syllables. One syllable at Chirp 0 is the original steady tone.
- **Audio in**: the stereo input is summed to mono and analysed: an autocorrelation (McLeod
  NSDF) pitch tracker on a decimated copy, a loudness follower, a sibilance measure (share of
  energy above 4 kHz) and an onset detector. They drive a free-running bird voice: tracked
  pitch x **Pitch Multiplier** is the syrinx pitch, loudness (relative to the recent peak,
  so input gain does not matter) is the pressure, and hiss and breath turn the tone into
  turbulence (**Hiss to Turbulence**). A syllable onset adds a fast pitch kick scaled by
  Chirp. **Voice Range** should sit near the middle of the speaker's pitch (the tracker
  covers 0.5-2.5x); **Gate** is relative to the recent peak; **Pitch Glide** smooths the
  trace. **Bird Mix** blends the bird with the dry input. MIDI notes are ignored.
- **Both**: the keyboard phrases and the audio-driven voice play together.

The tracker lags the input by about one analysis window (64 ms). It works best on clear
speech or slow singing; background noise and a badly set Voice Range degrade it. The same
idea as [birdspeech](https://birdspeech.mitpit.com/): keep only pitch and loudness,
throw away the harmonics and formants, and resynthesise them as a bird, here through a
physical syrinx model so regime, coupling and respiration still respond.

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

The panel follows the Magneto look: Source, Audio Input (live input and bird pitch plot,
level, state), Phrase (a schematic of the phrase the sliders describe), Voice (preset,
mute, level, timbre, noise, roughness, regime, harmonic, pitch offset, duration), Motion
(vibrato, bend, respiration, tremolo), Tract (formants, coupling, voice offset, trachea
length) and Master (distance, gain, randomize, reset).

The ten preset blocks are unchanged. The Source, Phrase and Audio Input controls are global
and are appended after the original parameters, so saved projects keep their indices;
older projects now open with three chirping syllables per note (Syllables 1, Chirp 0 restores
the old single tone).

### MIDI

| CC | Parameter |
|----|-----------|
| 1  | Vibrato Depth |
| 7  | Master Volume |
| 11 | Level |
| 71 | Roughness |
| 73 | Duration |
| 74 | Timbre |
| 76 | Vibrato Rate |
| 77 | Noise |
| 78 | Harmonic |
| 79 | Coupling |
| 80 | AM Depth |
| 81 | Syllables (1-16) |
| 82 | Chirp |
| 83 | Pitch offset |
| 84 | Variation |
| 85 | Respiration |
| 86 | Gap |
| 87 | Bird Mix |
| 91 | Distance |
| 94 | Contour Bend |

Pitch bend is applied per MIDI channel (2 semitones by default, set per channel with RPN 0), and voices are keyed by channel and note, so Retune's one-note-per-channel output bends each note on its own.

### Status

DSP core, MIDI phrases, audio analysis and the Magneto-style UI are implemented with core
tests (sweep depth and speed, syllable counts, pitch lock, tracker accuracy, audio-mode
pitch following, gate, mix, status outputs). Not yet verified in a host: audio routing and
latency in a DAW, and an ear pass over the presets with Chirp on.
