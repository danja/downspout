---
title: Beards
order: 211
bundle: beards.vst3
kind: Audio effect
category: processor
role: Granular texture processor
screenshot: /assets/plugins/beards.png
capabilities: [audio input, audio output, MIDI CC input]
summary: Granular texture processor after Mutable Instruments Beads — a 4 second stereo buffer replayed by up to 32 grains, with feedback, freeze and reverb, controllable from Drift and Conductor.
---

## Opinion

Best as a send on something sustained. A pad at Size 60%, Density 75%, Feedback 30%
turns into a slowly shifting cloud; a drum loop at short Size and high Density becomes
rhythmic grit. Put Drift on it and let Position and Density wander. The Tape quality
adds a dark, crunchy character that sits well under a mix.

## Functionality

Beards is a port of the grain engine in Emilie Gillet's Mutable Instruments Beads
(MIT). Stereo input is written into a 4 second buffer. Each grain reads from a point
Position seconds back, with a length set by Size (below a third it plays backwards), an
envelope set by Shape, a transposition set by Pitch, and a stereo pan. Density sets the
grain rate: a steady clock below 48%, random clouds above 52%, and nothing between
(use the Grain button). Four scatter controls randomise Position, Size, Shape and Pitch
per grain. Feedback recirculates the wet signal into the buffer, Freeze stops
recording, and Quality chooses Digital, Vintage (12 bit) or Tape (8 bit mu-law).

**Control from Drift and Conductor.** On the **MIDI in** channel, CC 1 Position, 2 Size,
3 Density and 4 Dry/Wet match Drift's default lanes, so Drift needs no setup; CC 5-12
reach Shape, Pitch, Feedback, Reverb and the scatters, and CC 64 freezes. On the
**Conductor** channel, CC 21 Density, 22 Energy (Dry/Wet), 23 Mutation (scatter) and
24 = 127 Reset (fires a grain). Both channels are off by default. CCs move the controls
but the panel does not follow.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Position | 0–100 % | 50 % | CC 1. Scatter randomises per grain |
| Size | 0–100 % | 50 % | CC 2. Below 33 % reverse |
| Shape | 0–100 % | 50 % | CC 5. Envelope plot shown on the panel |
| Pitch | −24 to +24 st | 0 | CC 6 |
| Density | 0–100 % | 75 % | CC 3, Conductor 21. 48–52 % is silent |
| Feedback | 0–100 % | 0 % | CC 7 |
| Dry/Wet | 0–100 % | 50 % | CC 4, Conductor 22 |
| Reverb | 0–100 % | 0 % | CC 8 |
| Freeze | off, on | off | CC 64 |
| Scatter (Position, Size, Shape, Pitch) | 0–100 % | 0 % | CC 10, 11, 12, 9; Conductor 23 |
| Quality | Digital, Vintage, Tape | Digital | |
| Grain | trigger | — | Conductor 24 = 127 |
| MIDI in | Off, 1–16, All | Off | |
| Conductor | Off, 1–16 | Off | |

### Status

Core DSP, deterministic tests, the VST3 target and the catalogue screenshot are
complete. The Beads delay mode and CV/gate inputs are not ported. Host validation in
REAPER remains.
