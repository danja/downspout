---
title: Ghost
order: 201
bundle: ghost.vst3
kind: Audio/MIDI effect
category: generative
role: Ghost-note generator
screenshot: /assets/plugins/ghost.png
capabilities: [audio input, MIDI input and output, host transport]
summary: Audio-triggered ghost notes in time with the transport — ghost drum beats on channel 10 or ghost notes on a selectable channel, with Drift CC control over feel.
---

## Opinion

Ghost fills the gap between a played part and a programmed one: feed it a dry
drum bus and it peppers the 16th grid with low-velocity snare ghosts that sit
behind the beat by the Drag amount. At 15 % drag it reads as a lazy drummer;
push past 40 % and it starts to flam, which is a feature on sparse material.

## Functionality

Ghost listens to incoming audio and inserts MIDI
note events quantised to the transport 16th grid. Onset accents jump to the
next slot; quieter fills land on off-16th slots with probability Density, only
while audio is present. The audio output stays silent unless Audio Thru is
switched on. Drums mode voices kick 36 on the quarter and ghost
snare 38 off it, always on channel 10; Notes mode walks a pentatonic-minor
ladder from Base Note on the selected channel.

### Parameters

| Parameter    | Range         | Default | Notes                                                                    |
|--------------|---------------|---------|--------------------------------------------------------------------------|
| Sensitivity  | 0–100 %       | 50 %    | Onset threshold; higher catches more; CC 1                               |
| Density      | 0–100 %       | 35 %    | Fill probability per off-16th slot; CC 2                                 |
| Velocity     | 1–127         | 90      | Accent velocity; fills sound at ~45–75 % of it; CC 3                     |
| Drag         | 0–100 %       | 15 %    | Ghost lateness inside the slot; CC 4                                     |
| Mode         | Drums, Notes  | Drums   | Drums forces ch 10; Notes uses Channel                                   |
| Channel      | 1–16          | 10      | Response channel in Notes mode                                           |
| Base Note    | 0–127         | 38      | Voice anchor (drums) / scale root (notes)                                |
| Pass MIDI    | off/on        | on      | Forward incoming MIDI to the output                                      |
| Audio Thru   | off/on        | off     | Pass incoming audio to the output (monitoring)                           |
| Seed         | 1–65535       | 7       | Deterministic fill/voice stream                                          |
| CC Channel   | 1–16          | 1       | MIDI channel for all four CC overrides                                   |

### Drift routing

Route Drift's MIDI output to Ghost's MIDI input. With default settings, all
four Drift lanes map directly: CC 1 → Sensitivity, CC 2 → Density, CC 3 →
Velocity, CC 4 → Drag. CC 0 disables the override for that parameter. Routing
CCs are consumed; all other MIDI passes through.

### Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER is pending.
