---
title: Fathom
order: 213
bundle: fathom.vst3
kind: Instrument
category: instrument
role: Drone synthesizer
screenshot: /assets/plugins/fathom.png
capabilities: [audio output, MIDI note and CC input]
summary: Four-voice drone synthesizer with five generator algorithms, a notch filter and a delay network with a frequency shifter in its loop — CC 1-4 line up with Drift's lanes.
---

## Opinion

A drone instrument rather than a keyboard one. Attack and release are long by default, the
sustain pedal holds a chord indefinitely, and the four voices sit apart in the stereo field.
The generator makes the raw cluster; the processor turns it into a slowly spiralling wash,
because every repeat in the feedback loop is moved a little up or down in pitch. Hold a fifth
and an octave, set **Shift** to a few hertz, and let the tail climb. Put Drift on it and Warp,
Span, Morph and Seed wander on their own. It is not for plucks or fast lines.

## Functionality

Modelled on the Vhikk X algorithm reference from FORGE TME
(<https://forge-tme.com/vhikk-x/algos_launch.html>): a generator, a filter, a VCA and a
delay-based processor with a frequency shifter in its feedback loop. This is an original
implementation of that signal flow, not a port.

**Generator** — five algorithms, sharing five controls that mean something different in each:

| Algorithm | What it is |
|-----------|------------|
| Analog | A band-limited saw and pulse; the pulse frequency-modulates the saw |
| Shaper | A sine carrier phase-modulated, ring-modulated and folded by a second sine |
| Sines | Eight sines, near-harmonic, one a hair off the fundamental so it beats |
| Table | Four wavetable oscillators scanning eight frames, offset by the seed |
| Cluster | Four oscillators on diatonic steps around the note; Morph scans across them |

**Filter** — a low-pass (**Field**) with two notches (**Notch**), per voice.

**Processor** — **Metallic** is a four-line feedback delay network; **Diffuse** is a stereo
ping-pong delay through allpass diffusers. **Time** sets the delay length, **Form** moves from
distinct echoes to a diffuse wash, **Feed** sets how long it rings, **Shift** moves each repeat
in pitch, **Mix** blends dry and wet.

**Voices** — four, panned apart. **Basis** transposes, **Glide** slides between notes, **Hold**
(sustain pedal, CC 64) keeps notes sounding after the keys come up. Pitch bend is +-2 semitones.

CC 1-4 are Warp, Span, Morph and Seed, which are Drift's default lane numbers; CC 5 is Fuse, 6
Field, 7 Level, 8 Notch, 9 Mix, 10 Time, 11 Form, 12 Feed, 13 Shift, 14 Basis.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Algorithm | Analog, Shaper, Sines, Table, Cluster | Sines | Changing it live clicks |
| Warp | 0-100 % | 30 % | CC 1 |
| Span | 0-100 % | 25 % | CC 2 |
| Morph | 0-100 % | 30 % | CC 3 |
| Seed | 0-100 % | 50 % | CC 4. Scans smoothly; repeatable |
| Fuse | 0-100 % | 0 % | CC 5 |
| Field | 40 Hz-20 kHz | 20 kHz | CC 6 |
| Notch | 0-100 % | 0 % | CC 8 |
| Processor | Metallic, Diffuse | Metallic | |
| Time | 10 ms-0.8 s | 58 ms | CC 10 |
| Form | 0-100 % | 60 % | CC 11 |
| Feed | 0-100 % | 50 % | CC 12 |
| Shift | -500 to +500 Hz | off | CC 13 |
| Mix | 0-100 % | 35 % | CC 9 |
| Basis | -24 to +24 st | 0 | CC 14 |
| Glide | 0-2000 ms | off | |
| Attack | 5-5000 ms | 800 ms | |
| Release | 10-10000 ms | 3 s | |
| Hold | Off, On | Off | Sustain pedal, CC 64 |
| Level | 0-100 % | 60 % | CC 7 |
| MIDI In | Off, 1-16, All | All | Channel filter for notes and CC |

### Status

Core DSP, deterministic tests, the VST3 target and the catalogue screenshot are
complete. Host validation in REAPER remains, and it has not been auditioned by ear.
