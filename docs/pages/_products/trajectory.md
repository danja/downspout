---
title: Trajectory
order: 212
bundle: trajectory.vst3
kind: Instrument
category: instrument
role: Polygon-bounce synthesizer
screenshot: /assets/plugins/trajectory.png
capabilities: [audio output, MIDI note and CC input]
summary: Eight-voice synthesizer whose oscillator is a point bouncing inside a regular polygon, with a full ADSR — six oscillator controls on CC 1-6, so Drift's lanes drive it with no setup.
---

## Opinion

A different kind of oscillator: instead of a waveform you pick a shape and a launch,
and the sound is whatever the bouncing point draws. Triangles are brash and metallic,
squares are close to a hollow pulse, and a dozen sides goes soft and organ-like. The
interesting part is the movement — put Drift on it and Sides, Start Position, Launch
Angle and Bounce Jitter wander, and the tone changes character without ever sounding
like a filter sweep. It is happiest on leads, basses and drones. It does not track
concert pitch exactly (see below), so tune by ear.

## Functionality

The oscillator is a point travelling in straight lines inside a regular polygon of 3 to
12 sides, reflecting off the edges. The waveform is `x * X Level + y * Y Level`. A note
sets the point's speed (`frequency * 4 / sampleRate`) and, at note-on, places it on the
boundary at **Start Position** and launches it along **Launch Angle**. **Bounce Jitter**
turns the heading by a small random angle at each bounce.

It is ported from the Trajectory experiment in the Flues project
(<https://danja.github.io/flues/trajectory/>). The oscillator and its six controls are
unchanged; the original's reverb is removed and its attack/release envelope is now a full
ADSR. Eight voices, each with its own point and envelope.

Pitch follows the note — an octave up doubles the rate — but the absolute pitch is a
property of the shape: a square launched at 45 degrees repeats at the note's frequency,
a hexagon sits a little flat, a dodecagon about a fourth flat. That is how the original
behaves and it has been kept.

The six oscillator controls are CC 1-6 in panel order and CC 7 is Level. Drift's four
lanes emit CC 1-4 by default, so they drive Sides, Start Position, Launch Angle and Bounce
Jitter without configuration; point a lane at CC 5 or 6 for X Level or Y Level.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Sides | 3-12 | 6 | CC 1 |
| Start Position | 0-360 deg | 0 | Read at note-on. CC 2 |
| Launch Angle | 0-360 deg | 45 | CC 3 |
| Bounce Jitter | 0-10 deg | 0 | Seeded, repeatable. CC 4 |
| X Level | 0-100 % | 0 % | CC 5 |
| Y Level | 0-100 % | 100 % | CC 6 |
| Attack | 1-1000 ms | 10 ms | |
| Decay | 5-3000 ms | 300 ms | Full-scale ramp time |
| Sustain | 0-100 % | 80 % | |
| Release | 10-4000 ms | 120 ms | |
| Velocity | 0-100 % | 50 % | 0 ignores note velocity |
| Level | 0-100 % | 70 % | CC 7 |
| MIDI In | Off, 1-16, All | All | Channel filter for notes and CC |

### Status

Core DSP, deterministic tests, the VST3 target and the catalogue screenshot are
complete. Host validation in REAPER remains.
