---
title: Box
order: 247
bundle: box.vst3
kind: Audio effect
category: processor
group: space
role: Wooden body resonator
screenshot: /assets/plugins/box.png
capabilities: [stereo audio input and output, MIDI CC input]
summary: Physical model of a wooden instrument structure — plate modes, soundhole and cavity driven from XY excitation and pickup positions. Wide ranges cover naturalisation and extreme resonant effects.
---

## Opinion

Box is for the gap between "this synth sounds like a signal" and "this sounds
like somebody is playing it in a room". Put it after a bare pluck, a keys patch
or a drum bus and start with Mix around 35 %. The default Guitar body adds a
wooden top and a small air resonance without announcing itself.

The interesting move is to treat the two XY pads as performance controls. Move
**Excitation** to where the sound would enter the body and **Pickup** to where a
microphone would sit; the same resonant bank changes character because the modal
weights change. For an effect, open the Soundhole, push Drive past 20 dB and use
a 0.05x or 4x body. Those settings are not realistic and are not meant to be.

## Functionality

The wooden structure is a simply supported rectangular plate. For mode `(m, n)`:

`f_mn = (pi/2) · sqrt(D / (rho·h)) · (m²/a² + n²/b²)`

where `D = E·h³ / (12·(1-nu²))`, `nu = 0.3`, `a` and `b` come from Body and
Size, `h` from Thickness, `E` from Stiffness and `rho` from Density. Twelve modes
are rendered as band-pass resonators; Damping sets their T60 and Q. The XY pads
apply `sin(m·pi·x)·sin(n·pi·y)` at both the drive and the read point, so nulling
one mode really removes it from the sound.

Guitar and Cabinet add a Helmholtz soundhole
`f = (c/2pi)·sqrt(A / (V·(t + 1.7r)))`, an air volume and two axial cavity modes.
Piano and Free Plate remove the air path. Drive soft-clips the excitation;
Wood Nonlinearity stiffens the panel with level. Both are deliberately
exaggerated effect controls, not calibrated material behaviour.

Stereo Width spreads and slightly detunes the two pickup positions. Media are
recomputed every 32 samples and glide over 5 ms, so CC moves stay click-free at
any host block size.

### Parameters

| Group | Parameters | Range / default | Notes |
|-------|------------|-----------------|-------|
| Structure | Body | Guitar / Piano / Cabinet / Free Plate | Sets geometry and whether air is enclosed |
| | Body Size | 0.05–4x / 1x | Frequency falls with size squared; CC 1 |
| | Thickness | 0.5–40 mm / 3 mm | Plate bending stiffness grows with h³ |
| | Stiffness | 0.5–40 GPa / 11 GPa | Wood to stiff composite |
| | Density | 100–1500 kg/m³ / 450 | Lighter plates resonate higher |
| | Damping | 0.02–8x / 1x | Modal T60 scales inversely; CC 2 |
| Air | Soundhole | 0–300 mm / 90 mm | Zero seals the box; CC 3 |
| | Air Volume | 1–2000 L / 15 L | Helmholtz and cavity resonance |
| | Body Depth | 20–500 mm / 100 mm | Cavity dimensions |
| Play | Drive | 0–36 dB / 6 dB | Soft-clips the excitation; CC 4 |
| | Wood Nonlinearity | 0–100 % / 15 % | Level-dependent stiffening |
| | Excitation X/Y | 0–100 % / 50, 35 % | Where the structure is driven |
| | Pickup X/Y | 0–100 % / 50, 50 % | Where it is heard |
| | Stereo Width | 0–100 % / 30 % | Pickup spread and detune |
| Integration | Mix / Output / Bypass | 35 % / 0 dB / off | Five-millisecond crossfade |
| | Seed / Randomise / CC Channel | 1 / trigger / Ch 1 | Reproducible new body |
| Read-only | Body Mode / Air Mode / Decay / Mode Q | derived | Live on the panel |

### Drift routing

Route Drift's MIDI output to Box's MIDI input. CC 1–4 overwrite the matching
host parameters on the selected CC Channel; other controllers are ignored.
Values use the same logarithmic track as the sliders, so a CC at 64 means the
same as a slider at 50 %.

| CC | Parameter |
|----|-----------|
| 1  | Body Size |
| 2  | Damping |
| 3  | Soundhole |
| 4  | Drive |

### State and lifecycle

Every setting is an ordinary automatable parameter, so DPF saves no extra
project snapshot. `activate()` and a sample-rate change clear only runtime filter
state; a saved body never resets. Randomise derives a new plausible structure
from Seed and leaves Mix, Output, Bypass and CC Channel alone.

### Status

Core DSP, deterministic tests, the VST3 target and the catalog screenshot are
complete. Visual acceptance of the screenshot is pending because this session
could not inspect the rendered image at full resolution; host listening in
REAPER is also pending.
