---
title: Bubbles
order: 22
bundle: bubbles.vst3
kind: Instrument
role: Water sound generator
screenshot: /assets/plugins/bubbles.png
capabilities: [audio output, MIDI input, host transport]
summary: Stereo ambient water synthesizer combining subtractive noise, physical bubble and drip modelling, body resonances, wave oscillator, and an LFO modulator — with seven water-environment presets.
---

## Functionality

Bubbles generates continuous stereo water textures without requiring a MIDI
note to play. Three synthesis layers are mixed per mode: a subtractive noise
chain (two-stage low-pass → bandpass extraction) for flow and turbulence; a
pool of up to 36 bubble voices and 16 drip voices driven by impulse-excited
decaying resonators; and a sinusoidal wave oscillator that amplitude-modulates
the mix to create rhythmic ocean or river surges. Four modal body resonances
model the acoustic character of a containing vessel. A stereo cross-coupled
delay adds spatial spread and a Padé-approximant tanh stage provides soft-clip
warmth.

When the host transport is running, the wave oscillator locks to one cycle per
bar at the host tempo. Without transport the oscillator runs at an
internally-computed rate derived from the current mode.

MIDI note-on gates the sound and biases bubble frequency and amplitude toward
the played note and velocity. Without MIDI input, Bubbles runs in ambient mode
with the gate held open.

### Modes

Seven environment presets weight the synthesis layers and set default wave
oscillator rates:

| # | Name | Character |
|---|------|-----------|
| 0 | Stream | Rushing noise, small bubbles, gentle ripple |
| 1 | River | Strong flow, cascade bubbles, rhythmic pulses |
| 2 | Ocean | Wave dominant, deep body resonance, sparse foam |
| 3 | Bubbles | Dense bubble events, minimal flow noise |
| 4 | Drips | Sparse drip impacts, quiet ambient noise |
| 5 | Rain | Dense drip rain, turbulent noise |
| 6 | Custom | Balanced blend of all layers — user-configurable |

### Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Mode | 0–6 | 6 (Custom) | Water environment preset |
| Flow | 0–1 | 0.50 | Noise flow intensity and wave amplitude |
| Turbulence | 0–1 | 0.40 | Bandpass roughness and whitecap energy |
| Size | 0–1 | 0.45 | Body/resonator size (0 = large/low, 1 = small/high) |
| Density | 0–1 | 0.50 | Bubble and drip spawn rate |
| Heat | 0–1 | 0.30 | Boiling energy: faster, smaller bubbles |
| Depth | 0–1 | 0.20 | Underwater low-pass character |
| Brightness | 0–1 | 0.55 | High-frequency filter content |
| Resonance | 0–1 | 0.55 | Resonator Q scaling |
| Randomness | 0–1 | 0.40 | Stochastic pitch jitter on events |
| Space | 0–1 | 0.35 | Stereo delay wet/feedback |
| Drive | 0–1 | 0.35 | Soft-clip saturation amount |
| Output | 0–1 | 0.80 | Post-saturation output level |
| Conductor Ch | 0–16 | 0 (off) | MIDI channel for Conductor CC reception |
| LFO Target | 0–6 | 0 (off) | Parameter to modulate: Off / Flow / Density / Brightness / Size / Heat / Randomness |
| LFO Shape | 0–4 | 0 (Sine) | Sine / Triangle / Ramp Down / Ramp Up / Square |
| LFO Rate | 0.05–20 Hz | 0.5 | Free-run oscillator rate |
| LFO Depth | 0–1 | 0.0 | Modulation depth (additive, clamped to parameter range) |
| LFO Sync | 0–1 | 0 (off) | Lock LFO phase to host transport |
| LFO Division | 0–7 | 2 (1/4) | Tempo division when synced: 1/1, 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |

### LFO modulator

The built-in LFO runs at control rate (every 64 samples) and applies additive
modulation to one target parameter, clamped to its [0, 1] range. When Sync is
on and the host transport is playing, the LFO phase is derived from the
absolute beat position so it restarts correctly after loop or seek. When
transport is stopped or Sync is off, the LFO free-runs at the Rate value.

For more complex or multi-parameter modulation, route
[Drift](/downspout/plugins/drift/) MIDI output into Bubbles — Drift's walk,
chaos, and audio-follower modes map naturally onto the CC addresses below.

### MIDI

| CC | Parameter |
|----|-----------|
| 1  | Flow |
| 2  | Turbulence |
| 3  | Density |
| 4  | Size |
| 5  | Heat |
| 6  | Depth |
| 7  | Output |
| 71 | Resonance |
| 74 | Brightness |
| 75 | Randomness |
| 76 | Space |
| 77 | Drive |

Note-on velocity scales bubble/drip amplitude. Note number biases bubble
resonator frequency relative to middle C. All-notes-off (CC 123) releases
the gate.

### Conductor integration

When **Conductor Ch** is set to a non-zero MIDI channel, Bubbles listens for
Conductor section-transition messages on that channel:

| CC | Conductor name | Bubbles control |
|----|---------------|-----------------|
| 21 | Density | `density` |
| 22 | Energy | `flow` |
| 23 | Mutation | `randomness` |
| 24 | Reset | Spawn burst (fires at value 127) |

### Status

DSP core, MIDI handling, LFO modulator, transport sync, Conductor integration,
and NanoVG panel are complete. Preset mode tuning and host validation are
ongoing. A screenshot is pending.
