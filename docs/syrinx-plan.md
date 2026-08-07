# Syrinx Plugin Plan

Syrinx is a DPF plugin that ports the Mindlin-Laje biophysical syrinx model from the [Lyrebird](https://github.com/danja/Lyrebird) project into a C++ audio plugin with a drumkit-style NanoVG UI. It models avian vocal production — the same physics that produces birdsong.

---

## DSP Core

The synthesis engine comes from `Lyrebird/app/src/lib/audio/worklets/syrinx-processor.js`. Key algorithms to port to C++:

### 1. Syrinx Oscillator (Mindlin-Laje ODE)
- Nonlinear differential equation modelling vocal fold + labial dynamics
- Explicit Euler integration at sample rate with 16–64× oversampling
- Two syringeal sides; optional two-voice coupling via shared tracheal pressure
- `beta` parameter clamped to [0.15, 16] for stability
- Per-syllable `gamma` (time-scale) keeps pitch in well-behaved parameter space

### 2. Vocal Tract Filter
- Pitch-tracking bandpass biquad (RBJ), retuned every 32 samples
- Centre frequency follows instantaneous f0 from pitch contour
- Q ~5–9, constant 0 dB peak gain

### 3. Formant Bank (optional)
- Up to 4 fixed-frequency resonances per side (bandpass biquads, Q 0.7–40)
- Used for fricative/rough syllables where pitch-tracking band is insufficient

### 4. Respiration Model (Fainstein-Goller-Mindlin)
- Two-state air-sac dynamics replacing simple envelope
- Precomputed at note start; drives tract pressure multiplicatively via √(ps)
- Fills internal silences with mini-breath gestures

### 5. Source Modifiers
- **Noise injection** — turbulence at labia (xorshift32 PRNG)
- **Pressure roughness** — 140 Hz lowpass-filtered perturbation, depth 0.12
- **Timbre** — gain scaling: 1.0 = pure, 1.25 = reed, 1.75 = buzz

### 6. Amplitude Envelope
- Attack → hold (75 % of decay) → exponential release
- Peak-to-peak swing normalisation per syllable

### 7. Master Chain (same topology as Lyrebird)
- Voice gain → air filter (lowpass 12 kHz, distance-modulated) → dry/wet split
- Wet → short convolution reverb (~1.6 s exponential impulse)
- Master → DynamicsCompressor limiter (−3 dB threshold, 20:1, 3 ms attack, 120 ms release)

---

## Parameters

Eighteen continuous + two boolean parameters:

| ID | Name | Range | Default | Notes |
|----|------|--------|---------|-------|
| 0 | Pitch | 0–1 (exp → 80–2000 Hz) | 0.35 | Base f0; contour applied relative to this |
| 1 | Duration | 0.05–2.0 s | 0.25 | Syllable length |
| 2 | Level | 0–1.4 | 0.8 | Source amplitude |
| 3 | Timbre | 0–1 (1.0/1.25/1.75 blend) | 0.0 | 0=pure, 0.5=reed, 1=buzz |
| 4 | Noise | 0–1 | 0.1 | Turbulence / raspiness |
| 5 | Roughness | 0–1 | 0.0 | Pressure roughness depth |
| 6 | Vibrato Rate | 0–15 Hz | 5.0 | |
| 7 | Vibrato Depth | 0–100 cents | 20 | |
| 8 | Contour Bend | −1–1 | 0.0 | Pitch contour arc; 0=flat, +1=upsweep, −1=downsweep |
| 9 | Harmonic | 0–2 | 1.0 | Richness / harmonic spread |
| 10 | Respiration | 0–1 | 0.0 | Blend towards air-sac dynamics |
| 11 | Formant 1 | 200–8000 Hz | 1200 | Fixed resonance 1 |
| 12 | Formant 2 | 200–8000 Hz | 2500 | Fixed resonance 2 |
| 13 | Formant Q | 0.7–20 | 5.0 | Shared Q for formant bank |
| 14 | Coupling | 0–1 | 0.0 | Two-voice coupling strength |
| 15 | Distance | 0–1 | 0.2 | Air absorption + reverb mix |
| 16 | Master Volume | 0–1 | 0.75 | Output gain |
| 17 | AM Rate | 0–30 Hz | 0.0 | Amplitude modulation (trill/rattle) |
| 18 | Mute | bool | false | |
| 19 | Polyphony | 1–8 | 4 | Max simultaneous voices |

---

## MIDI

### Note-On → Pitch

Syrinx is a chromatic instrument: MIDI note number maps to a base frequency that overrides the Pitch slider.

```
f0 = 440 * 2^((note - 69) / 12)   Hz
```

This lets it be played melodically from a keyboard. Middle C (60) ≈ 261 Hz falls comfortably in the bird-call range when the Pitch slider acts as a transpose/fine-tune offset.

Velocity → amplitude scaling (0–127 → 0.0–1.4 level, respecting the Level parameter as ceiling).

### Chord / Polyphony

Up to `Polyphony` voices play simultaneously. New notes steal the oldest completed voice. Each voice runs its own ODE state independently, allowing overlapping syllables.

### CC Mappings

| CC | Parameter | Notes |
|----|-----------|-------|
| 1  | Vibrato Depth | Mod wheel — classic |
| 7  | Master Volume | Standard volume |
| 10 | Pan | Stereo position |
| 11 | Expression | Level scaling |
| 71 | Roughness | Timber/filter |
| 73 | Attack (Duration) | Envelope-like |
| 74 | Timbre | Brightness/reed-ness |
| 76 | Vibrato Rate | LFO rate |
| 77 | Noise | Turbulence |
| 78 | Coupling | Two-voice effect |
| 91 | Distance (Reverb) | Wet level |
| 94 | Contour Bend | Pitch gesture shape |

All CC values normalise linearly to parameter range unless otherwise noted.

### Pitch Bend

Pitch bend (±2 semitones default) modulates f0 continuously across all active voices.

---

## UI Layout (NanoVG, drumkit-style)

Target canvas: **1240 × 760 px** (same as drumkit).

```
┌─────────────────────────────────────────────────────────────────┐
│  SYRINX          [Preset ▼]  [◀]  [▶]  [RANDOMIZE]            │  72px header
├────────────────────────────────┬────────────────────────────────┤
│                                │                                │
│   PRESET STRIPS  (10 cols)     │   VOICE EDITOR                 │
│   each ~115px wide             │   horizontal sliders for       │
│   coloured header bar          │   selected preset's params     │
│   preset name                  │   (Pitch, Timbre, Noise,       │
│   mini pitch-contour preview   │    Roughness, Vibrato, etc.)   │
│   level fader (vertical)       │                                │
│   mute button                  │                                │
│                                │                                │
├────────────────────────────────┴──────────────┬─────────────────┤
│  CONTOUR EDITOR (mini pitch curve display)    │  MASTER PANEL   │
│  draggable nodes, read-only in preset mode    │  Distance       │
│                                               │  Coupling       │
│                                               │  AM Rate        │
│                                               │  Volume         │
└───────────────────────────────────────────────┴─────────────────┘
```

### Preset Strips

Ten preset slots, each showing:
- Coloured header bar (species colour palette)
- Preset name (e.g. "Wren", "Thrush", "Warbler", "Custom 1"…)
- A small static pitch-contour preview (sparkline of the bend arc)
- Vertical level fader
- Mute toggle button

Clicking a strip selects it; the Voice Editor panel updates to show that preset's parameter sliders.

### Voice Editor

Same horizontal-slider pattern as drumkit's voice editor:
- Label left, value right (colour-coded by parameter family)
- Drag thumb or scroll to adjust
- Parameters shown: Pitch, Duration, Timbre, Noise, Roughness, Vibrato Rate, Vibrato Depth, Contour Bend, Respiration, Harmonic

### Contour Editor

A small canvas (~400×80 px) with a cubic Bézier curve representing the pitch trajectory. In preset mode it is read-only (preview). A future "Edit" toggle could allow dragging control points.

### Master Panel

Four sliders: Distance, Coupling, AM Rate, Master Volume. Identical drawing style to drumkit's master panel.

### Randomize Button

Pressing Randomize generates a new random parameter set for the currently selected preset slot:
- Pitch: log-uniform 80–1600 Hz
- Duration: uniform 0.05–0.8 s
- Timbre, Noise, Roughness, Respiration, Coupling: uniform 0–1 (with Roughness and Coupling biased low)
- Vibrato Rate: uniform 2–12 Hz
- Vibrato Depth: uniform 0–60 cents
- Contour Bend: uniform −0.8–0.8
- Harmonic: uniform 0.5–1.8
- Formants: log-uniform within valid range

Randomize preserves Level and Mute.

---

## 10 Built-in Presets

| # | Name | Character |
|---|------|-----------|
| 1 | Wren | Fast, complex, high-frequency warble |
| 2 | Thrush | Rich, melodic phrases, formant emphasis |
| 3 | Warbler | Sustained tones with slow vibrato |
| 4 | Finch | Short bright chips, high pitch |
| 5 | Robin | Mid-range, pure, rounded contour |
| 6 | Nightjar | Low raspy churn, noise + roughness |
| 7 | Pigeon | Soft coo, respiration-driven, low coupling |
| 8 | Hummingbird | Ultra-fast AM trill, high pitch |
| 9 | Starling | Buzzy reed timbre, irregular roughness |
| 10 | Custom | Blank/flat default — user starting point |

---

## File Structure

```
plugins/syrinx/
├── CMakeLists.txt
├── include/
│   ├── syrinx_engine.hpp        # Voice pool, parameter accessors, MIDI routing
│   ├── syrinx_params.hpp        # ParameterSpec array, CC map, preset table
│   └── dsp/
│       ├── SyrinxOscillator.hpp # Mindlin-Laje ODE solver + oversampling
│       ├── TractFilter.hpp      # Pitch-tracking bandpass biquad
│       ├── FormantBank.hpp      # Fixed-frequency resonance bank
│       ├── RespirationGesture.hpp # Air-sac precomputation
│       ├── TwoVoiceSource.hpp   # Coupled oscillators + fractional delay
│       └── SyrinxVoice.hpp      # Assembles DSP chain per voice
├── src/
│   ├── syrinx_engine.cpp
│   └── dpf/
│       ├── DistrhoPluginInfo.h  # Plugin metadata (CLAP/VST3/AU)
│       ├── SyrinxPlugin.cpp     # DPF Plugin subclass, MIDI, run()
│       └── SyrinxUI.cpp         # NanoVG UI (~600 lines, drumkit pattern)
└── presets/
    └── syrinx_presets.hpp       # Embedded default parameter tables
```

Reused from drumkit (copy/symlink or shared library):
- `BiquadFilter.hpp`
- `ADEnvelope.hpp`
- `ReverbModule.hpp`
- `DCBlocker.hpp`
- `NoiseGenerator.hpp`
- `Distortion.hpp`

---

## Implementation Sequence

1. **Port DSP** — translate `syrinx-processor.js` to `SyrinxOscillator.hpp`, starting with single-voice no-oversampling, then add oversampling, tract filter, noise, formants, respiration, two-voice.
2. **Voice class** — wrap oscillator + envelope + tract into `SyrinxVoice` with `trigger()` / `process()`.
3. **Engine** — voice pool (8 voices), parameter table, MIDI handler, master chain.
4. **Plugin wrapper** — copy `DrumkitPlugin.cpp` pattern, adapt parameter count and MIDI note→frequency mapping.
5. **Preset table** — tune 10 presets by hand (or derive from Lyrebird's `inventory.json`).
6. **UI** — adapt `DrumkitUI.cpp`; replace instrument strips with preset strips, add contour preview sparkline, add Randomize button.
7. **CC map** — wire CC handling in plugin wrapper.
8. **Randomize** — implement parameter randomisation logic in UI or engine.

---

## Open Questions

- **Oversampling factor** — 16× is safe but expensive at low latency; make it a parameter (8/16/32/64×) or fix at 32×.
- **Contour representation** — store as array of (time, freq) pairs per preset vs. a single Bézier bend scalar. The Bézier approximation is simpler for UI but loses complex multi-segment contours.
- **Inventory.json integration** — Lyrebird ships a corpus of real syllable descriptors. These could seed the 10 presets automatically, giving biophysically grounded starting points.
- **Polyphony vs. ambient chorus** — Lyrebird has a Poisson-scheduled ambient chorus mode. This could be an optional "flock mode" triggered by a long MIDI note hold or a dedicated toggle, scheduling multiple overlapping voices at random intervals.
- **Two-voice coupling** — requires a fractional-delay circular buffer per voice pair; adds complexity. Could be deferred to v2 and faked with a short comb filter for v1.
