# Bubbles

Stereo water sound generator using a hybrid of subtractive synthesis and
physical modelling. Produces continuous ambient water textures without MIDI;
MIDI note-on/off optionally gates and modulates the sound.

## Water modes

| Mode    | Character |
|---------|-----------|
| Stream  | Rushing brook, high-frequency noise, rapid small bubbles |
| River   | Strong flow, cascade turbulence, rhythmic pulses |
| Ocean   | Wave-amplitude LFO, deep body resonance, sparse foam |
| Bubbles | Dense stochastic bubble events, modal body resonance |
| Drips   | Sparse dual-resonator drip impacts, quiet ambience |
| Rain    | Dense drip rain on a surface, turbulent noise texture |
| Custom  | Balanced blend; all parameters contribute equally |

## Parameters

| Parameter   | Default | Description |
|-------------|---------|-------------|
| Mode        | Custom  | Water environment (see table above) |
| Flow        | 0.50    | Noise flow intensity / wave amplitude |
| Turbulence  | 0.40    | Bandpass roughness and whitecap energy |
| Size        | 0.45    | Body / resonator size (0=large/low, 1=small/high) |
| Density     | 0.50    | Bubble and drip spawn rate |
| Heat        | 0.30    | Boiling energy: faster, smaller bubbles |
| Depth       | 0.20    | Underwater LP character |
| Brightness  | 0.55    | High-frequency filter content |
| Resonance   | 0.55    | Resonator Q scaling |
| Randomness  | 0.40    | Stochastic pitch jitter on events |
| Space       | 0.35    | Stereo delay wet/feedback |
| Drive       | 0.35    | Saturation amount |
| Output      | 0.80    | Output level |

## DSP architecture

- **Subtractive layer**: white noise → two-stage one-pole LP → bandpass
  extraction + two turbulence bandpass bands.
- **Physical modelling layer**: up to 36 bubble voices (impulse-excited decaying
  resonators) and up to 16 drip voices (dual-resonator, two-stage decay); four
  modal body resonances model the acoustic container.
- **Wave layer**: sinusoidal LFO amplitude-modulates the mix for ocean/river
  wave rhythms.
- **Depth**: one-pole LP per channel simulates underwater propagation.
- **Spatial**: cross-coupled stereo delay with configurable wet level and
  feedback.
- **Saturation**: soft-clip (tanh) for warmth and limiting.

## Status

v0.1.0 — initial implementation. Screenshot and UI refinement pending.
