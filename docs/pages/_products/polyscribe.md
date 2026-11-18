---
title: Polyscribe
order: 245
bundle: polyscribe.vst3
kind: Audio/MIDI effect
category: generator
group: analysis
role: Polyphonic audio-to-MIDI generator
screenshot: /assets/plugins/polyscribe.png
capabilities: [audio input, audio output, MIDI note output, live status parameters]
summary: Turns audio into MIDI notes. Choose Melody, Bass, Chords or Drums, then shape sensitivity, note range, polyphony, gate, release, velocity and dynamics.
---

## Opinion

Polyscribe is the broad sibling of Qune. Qune follows one pitch and shifts the
audio; Polyscribe listens for several notes and writes a MIDI part. Feed it a
soloed bass and it follows the low line; feed it a played chord and it writes the
chord to a synth; feed it a drum bus and it writes kick, snare and hat notes on
their usual GM numbers. Audio output stays silent by default, so it can sit on a
source track while the MIDI goes elsewhere.

The four input types matter more than any single slider. They set both the note
selection strategy and the band it searches, which prevents a high harmonic or
a competing instrument from stealing the result. The Live Status panel makes the
detection legible: last note, active count, velocity, input level and a flash on
a fresh note-on.

## Functionality

Stereo input is summed to mono for analysis. Pitched modes use a 4096-sample Hann
window every 512 samples, with Goertzel magnitudes for MIDI notes 24-120, local
peaks and harmonic suppression. **Melody** takes the strongest peak with
hysteresis, **Bass** the lowest in C2-C4, and **Chords** up to **Max Notes**
simultaneous peaks, retaining notes that are already sounding so phase drift
does not cause retriggers. **Low Note** and **High Note** narrow those ranges;
they do not extend them.

**Drums** uses the most recent 1024 samples and three probes at 65 Hz, 294 Hz and
4.2 kHz. Separate arming, envelope, peak and 60 ms cooldown state per band turns
onsets into GM kick 36, snare 38 and closed hat 42. Channel 10 is the convention
for drums, but **MIDI Channel** is editable in every mode.

A detected note stays on for **Gate** plus **Release** after it disappears.
Changing type, channel, Transpose or MIDI Out releases sounding notes before the
new setting takes effect. There is no MIDI input and no CC map; all user controls
are host parameters, and five output parameters expose live status.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Input Type | Melody, Bass, Chords, Drums | Melody | Chooses note selection and search band |
| Sensitivity | 0-100 % | 65 % | Detection floor |
| Low Note | 24-96 | 36 | Intersected with the type's range |
| High Note | 36-120 | 84 | Intersected with the type's range |
| Max Notes | 1-8 | 4 | Chords only |
| Gate | 0-100 ms | 20 ms | Minimum note-on length |
| Release | 0-500 ms | 120 ms | Hold after the note disappears |
| Velocity | 1-127 | 100 | Base velocity |
| Dynamics | 0-100 % | 35 % | Level-to-velocity variation |
| Transpose | -24 to +24 st | 0 | Whole semitones |
| MIDI Channel | 1-16 | 1 | Drums conventionally use 10 |
| MIDI Out | Off, On | On | Enables generated messages |
| Audio Thru | Off, On | Off | Mirrors input to output |

### Wiring

- **Audio:** 2 in, 2 out. Analysis always reads the input; the outputs are silent
  unless Audio Thru is on. Non-finite samples are replaced with zero.
- **MIDI:** output only. Route `polyscribe.vst3` before a synth, or use a MIDI
  routing item to send its notes to another track. It does not consume MIDI.
- **Transport:** ignored. Detection follows audio continuously.
- **Latency:** not reported, because audio is never delayed. The first pitched
  result needs a full 4096-sample window, about 93 ms at 44.1 kHz, so note timing
  follows detection rather than the original attack.

## Status

Core tests, the VST3 build and bundle-list wiring are complete. A screenshot was
captured at the declared 920x720 size, but this environment could not inspect
the PNG pixels, so visual acceptance is pending. Host listening and MIDI-routing
validation in REAPER remain. See `plugins/polyscribe/docs/design.md` for the
algorithms and the full test/validation gap.
