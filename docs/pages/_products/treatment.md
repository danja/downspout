---
title: Treatment
order: 205
bundle: treatment.vst3
kind: Audio effect
category: processor
role: Acoustic treatment panel
screenshot: /assets/plugins/treatment.png
capabilities: [audio input and output, MIDI CC input]
summary: Physically modelled acoustic treatment panel — a mass-air-mass absorber whose resonance and absorption come from real panel geometry and fill.
---

## Opinion

Treatment is at its best as the thing you put on a bus that sounds like it is
in the wrong room. Reach for it on a bass-heavy mix that has lost its foundation,
or on a drum bus that is boomy but not really. It is a panel, not a tame, so it
works hardest in the low mid and leaves the top alone.

Start at the defaults and listen to what the readouts say: a 100 mm cavity with
a 50 mm gap puts the resonance around 173 Hz, which is where most unwanted
energy actually is. Then move Flow Resist and listen for the sweet spot. Both
too open and too dense absorb much less, and finding that by ear is the thing
worth doing once.

Randomise is for finding panels quickly while writing, not for playing. Save the
one you like.

## Functionality

A facing sheet of surface mass in front of a porous fill, with an air gap to the
wall. That is a mass-air-mass resonator, and Treatment computes it from the
physics rather than from a tone control: resonance follows
`w0 = c·sqrt(rho0 / (m·D))`, and absorption peaks where the fill's flow
resistance matches the characteristic impedance of air, which means the panel
gets *worse* if you push the fill too far in either direction. Cavity and Air Gap
are in millimetres, Facing Mass in kg/m² and Flow Resist in Rayl/m, because
that is how panels, facings and fills are actually specified.

The panel removes energy at its resonance and again above the fill's diffusion
corner. Amount scales the absorption itself, so at 0 % the plugin is
bit-transparent rather than a mix, and the response stays monotonic as you sweep
it. Nothing in the chain can ever boost.

### Parameters

| Parameter   | Range         | Default   | Notes                                          |
|-------------|---------------|-----------|------------------------------------------------|
| Cavity      | 20–400 mm     | 100 mm    | Fill depth; CC 2                               |
| Air Gap     | 0–400 mm      | 50 mm     | Gap to the wall; CC 3                          |
| Facing Mass | 0.2–8 kg/m²   | 0.8       | Facing sheet density; CC 4                     |
| Flow Resist | 1–60 kRayl/m  | 8 kRayl/m | Fill resistivity; CC 5. Match ≈ 4.1 at default cavity |
| Amount      | 0–100 %       | 100 %     | How much absorption; CC 1                      |
| Bypass      | off/on        | off       | DSP keeps running, so toggling is click-free   |
| Seed        | 1–9999        | 1         | Drives Randomise                               |
| Randomise   | trigger       | —         | Builds a new plausible panel                   |

The panel reports the derived total depth, resonance, Q, peak absorption and
fill diffusion corner, and they update live as the geometry moves.

### Drift routing

Route Drift's MIDI output to Treatment's MIDI input. With default settings CC 1
→ Amount, CC 2 → Cavity, CC 3 → Air Gap, CC 4 → Facing Mass, CC 5 → Flow
Resist. CC 1–4 are Drift's default lanes; each control's CC number is a dropdown
in the panel and `off` disables that override. CC Channel selects which MIDI
channel is listened to.

### Randomise

Draws a new cavity, gap, facing mass and flow resistivity from the seed, rounded
to values someone would actually pick. It advances the seed so a second press
differs, and it is reproducible from the seed alone. Amount and Bypass are left
alone, so randomising mid-performance does not change how much treatment you
have.

### Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalog
screenshot are complete. Host validation in REAPER is pending.
