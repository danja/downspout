# FlueSynth Driver

FlueSynth Driver is a VST3 MIDI controller plugin that provides a complete UI for an external
[flues-synth](https://github.com/danja/flues) instance running on Raspberry Pi or any other host.
It sits between a MIDI note source and the synth, forwarding notes and injecting control-change
messages for every flues-synth parameter from a single panel.

---

## Signal flow

```
DAW track (notes)  ─┐
Conductor (CC 20-24)─┤→ FlueSynth Driver ──→ MIDI out ──→ flues-synth ──→ audio
                     │
                UI controls ─┘ (generate CCs)
```

The plugin has **no audio I/O** (two silent dummy outputs exist for VST3 compatibility).
Its only meaningful output is a MIDI stream.

---

## Quick start

1. Insert **FlueSynth Driver** on a MIDI-only track.
2. Set **Out Ch** to the MIDI channel your flues-synth instance is listening on.
3. Route your note source (MIDI keyboard, MIDI clip, BassGen, etc.) into the plugin's MIDI input.
4. Ensure **Pass In** is on — notes and any non-Conductor CCs are forwarded unchanged.
5. Adjust synth parameters from the UI; each change immediately emits the corresponding CC.
6. Save the DAW project — all parameter values are stored in the plugin state.

---

## Routing controls

| Control | Range | Description |
|---------|-------|-------------|
| **Out Ch** | 1–16 | MIDI channel for all outgoing CCs and forwarded notes |
| **Cond Ch** | 0–16 (0 = off) | Listen for Conductor section commands on this channel |
| **Pass In** | on/off | Forward incoming MIDI to the output unchanged |
| **Panic** | trigger | Send All Notes Off on all 16 channels + reset all CCs to defaults |

---

## Oscillator — Disyn

| Parameter | CC | Range | Notes |
|-----------|----|-------|-------|
| Algorithm | 16 | 0–17 | Sine, Square, Tri, Saw, RevSaw, Pulse, Noise, FM, AM, Ring, Phase, Fold, Wrap, Clip, Crush, Shift, Comb, Karplus |
| P1 | 17 | 0–1 | Algorithm-specific parameter 1 |
| P2 | 18 | 0–1 | Algorithm-specific parameter 2 |
| P3 | 19 | 0–1 | Algorithm-specific parameter 3 (algorithms 7–17) |
| Gain | 7 | 0–1 | Master output level |

---

## Physical model

| Parameter | CC | Range | Notes |
|-----------|----|-------|-------|
| Interface Type | 24 | 0–11 | Pluck, Hit, Reed, Flute, Brass, Bow, Bell, Drum, Crystal, Vapor, Quantum, Plasma |
| Intensity | 1 | 0–1 | Excitation strength / interface intensity |
| Tuning | 26 | −12–+12 st | Pitch offset in semitones |

---

## Formants

| Parameter | CC | Range |
|-----------|----|-------|
| F1 Jaw | 71 | 200–1000 Hz |
| F2 Tongue | 10 | 500–3000 Hz |
| F3 Lips | 74 | 1500–4000 Hz |
| F4 Quality | 75 | 2500–4500 Hz |

---

## Vocal modes

Boolean toggles (CC value ≥ 64 = on).

| Parameter | CC | Effect |
|-----------|----|--------|
| Nasal | 80 | Nasal resonance at 250 Hz |
| Sing | 81 | Vibrato (5.5 Hz, ±1.5%) |
| Shout | 82 | Formant boost (+15%) |
| Fry | 83 | Vocal fry (f₀/2 subharmonic) |

---

## Envelope

| Parameter | CC | Range | Mapping |
|-----------|----|-------|---------|
| Attack | 73 | 0.001–1.0 s | Exponential |
| Release | 72 | 0.01–3.0 s | Exponential |

---

## Effects

| Parameter | CC | Range | Notes |
|-----------|----|-------|-------|
| Noise Level | 20 | 0–1 | White noise generator |
| DC Level | 21 | 0–1 | DC offset source |
| Delay 1 FB | 28 | 0–1 | Delay line 1 feedback |
| Delay 2 FB | 29 | 0–1 | Delay line 2 feedback |
| Delay Ratio | 27 | 0.5–2.0 | Delay line 2 frequency ratio (exponential) |
| Filter FB | 30 | 0–1 | State-variable filter feedback return |

---

## Filter

| Parameter | CC | Range | Notes |
|-----------|----|-------|-------|
| Filter Freq | 32 | 20–20 000 Hz | Exponential mapping |
| Filter Q | 33 | 0.1–10 | Exponential mapping |
| Filter Shape | 34 | 0–1 | 0 = LP, 0.5 = BP, 1 = HP |

---

## LFO

| Parameter | CC | Range | Notes |
|-----------|----|-------|-------|
| Rate | 36 | 0.1–20 Hz | Exponential mapping |
| AM/FM Depth | 37 | −1–+1 | Negative = AM, positive = FM |

---

## Trajectory (Program 2 only)

These controls only take effect when flues-synth is in **Trajectory Polygon** mode (Program 2).
They share CC numbers with Envelope and Formant parameters — sending a trajectory parameter
overrides the corresponding envelope/formant CC until the other parameter is moved.

| Parameter | CC | Shared with | Range |
|-----------|----|-------------|-------|
| Sides | 73 | Attack | 3–24 (integer) |
| Start Pos | 72 | Release | 0–360° |
| Start Angle | 28 | Delay 1 FB | 0–360° |
| Jitter | 30 | Filter FB | 0–10° |
| Clip | 74 | F3 Lips | 0–1 |
| Mix X | 71 | F1 Jaw | 0–1 |
| Mix Y | 1 | Intensity | 0–1 |

When using Trajectory mode, leave the conflicting normal parameters at their default positions to
avoid unintended CC collisions.

---

## Conductor integration

Set **Cond Ch** to the MIDI channel that Conductor outputs on (Conductor's **MIDI channel**
parameter, default 16). The plugin intercepts Conductor's section commands and maps them to
flues-synth parameters:

| Conductor CC | Default # | Mapped to |
|-------------|-----------|-----------|
| Scene | 20 | Interface Type (physical model selection, 0–11) |
| Density | 21 | Disyn P1 |
| Energy | 22 | Master Gain |
| Mutation | 23 | Disyn Algorithm (0–17) |
| Reset | 24 | Panic (all notes off + default CCs) |

Conductor CCs on the configured channel are consumed and not forwarded to flues-synth as raw CCs.
The driver translates them into the appropriate synth parameters and emits the correct CC numbers.

If Conductor's CC numbers have been changed from their defaults (20–24), the driver will not
recognise them — keep Conductor at its default CC assignments.

---

## CC conversion formulas

The UI parameters are stored in their semantic ranges (Hz, seconds, semitones) and converted
to MIDI CC 0–127 at emit time:

- **Linear**: `CC = round((value − min) / (max − min) × 127)`
- **Exponential** (formants, attack, release, filter, LFO): `CC = round(log(value/min) / log(max/min) × 127)`
- **Boolean** (vocal modes): `CC = value ≥ 0.5 → 127, else 0`
- **Discrete integers** (algorithm, interface type): `CC = round(value / maxValue × 127)`

Redundant CCs (same value as last sent) are suppressed to avoid spamming the MIDI bus.
After **Panic**, all CCs are treated as unsent and will be re-emitted on the next parameter
change or on the next activate.

---

## CC conflict with MK-449 legacy mapping

If `FLUES_MK449_MAP=1` is set in the synth environment, some CCs are remapped on the synth
side. This plugin does not implement the legacy map and targets the standard CC assignments
documented above.

---

## Automation

All synth parameters are automatable in the host. Parameter ranges are expressed in their
semantic units (Hz, seconds, semitones) so host automation curves are meaningful.

The **Panic** parameter is a momentary trigger — automating it to 1 fires panic,
automating to 0 resets it. It does not latch.

---

## Building

```bash
cmake -B build -DDOWNSPOUT_BUILD_FLUES_SYNTH_DRIVER=ON
cmake --build build --target flues_synth_driver
```

Core tests:

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build --target downspout_flues_synth_driver_core_tests
ctest --output-on-failure -R flues_synth_driver
```
