---
title: DrumGen
order: 70
bundle: drumgen.vst3
kind: MIDI drum generator
category: generative
group: rhythm
role: Drum pattern generator
screenshot: /assets/plugins/drumgen.png
capabilities: [MIDI output, MIDI input, host transport, pattern variation, fills, conductor control]
summary: Transport-aware MIDI drum generator with fills, style modes, Breakbeat/Jazz/Fugue genres, and sparse pulse options.
---

## Opinion

I'm pleased wit this one, it does its job admirably. The UI could probably do with tweaking a little, but that's low priority, it works.

## Functionality

DrumGen emits MIDI drum patterns that stay aligned with host transport. It can
create new patterns, mutate the current pattern, target fills, and switch
between straight, folk-oriented, breakbeat-inspired, Jazz, and sparse Fugue
metrical vocabularies.

### Pattern Mix

The right-hand **Pattern Mix** sliders place hits or change their probability;
none are audio levels. **Kick Hits**, **Snare/Clap Hits**, **Hat Hits**,
**Percussion Hits** (bell/clave/bash), **Fill Tom Hits** and **Crash/Bash Hits**
shape the corresponding lanes, alongside Density, Variation, Fill Amount, Vary
and Seed. **New**, **Mutate** and **Fill** regenerate the pattern: the panel
flashes `UPDATED` and shows a local `Gen N` count so a change is visible even
when only MIDI out is connected.

### Conductor integration

DrumGen accepts MIDI input for Conductor CC control. Set **Conductor Ch**
(0 = off, 1–16) to the channel Conductor uses for its output (default 16).
Conductor CC 21 drives density, CC 22 drives variation, CC 23 drives mutation
rate, and CC 24 (value 127) triggers a new pattern on each section boundary.
**Conductor Scene** is an appended on/off parameter (default Off). When On,
CC 20 maps Conductor's editable blocks to Fill: Intro 0.10, Theme 0.30,
Develop 0.65, Break 1.0, Reprise 0.50 and Coda 0.20; Null leaves Fill
unchanged. It is ignored by default so existing projects behave exactly as before.
The effective values of CC 21–23 appear as the read-only **Live Density**, **Live Variation**
and **Live Vary** parameters (`status_density`, `status_variation`, `status_vary`), which the
panel mirrors onto the matching sliders; DPF/VST3 does not push ordinary DSP parameter
changes to the UI. See [MIDI Mapping](../../midi-mapping.md).

### Automaton layer

An optional cellular-automaton layer evolves chosen lanes so a pattern keeps changing without
being regenerated. Its three selectors sit in the right-hand **Automaton** section, leaving the
Conductor chooser near the top of the Pattern panel. Pick an **Automaton rule** (Off, or Wolfram rule
30, 90, 110, 150, 18, 54, 60, 22, 105 or 126) and what it **plays on**: Hats, Percussion (bash,
cowbell, clave), Toms, all three together, or every lane.

The pattern as generated is generation 0 and plays on the first pass. On each later pass a targeted
lane plays the next row of a one-dimensional automaton (wrapping at the pattern length) seeded from
that lane's own hits, so hats thin out, fill in and shift in ways that are structured rather than
random. New hits take the lane's average velocity and surviving hits keep theirs. A lane with no hits
stays silent. After 64 generations a lane returns to the pattern. Kick, clap, snare and crash are
never touched unless you choose All lanes.

The pass number comes from the playback position, so loops, restarts and offline renders agree, and
the stored pattern is never altered (New, Mutate and Vary all still work on it). Rule 90 gives
symmetric, self-similar shapes, 30 and 110 are busier and less predictable, and 150 and 105 are
dense. **Generation every** (1-8 loops) holds each generation for that many loops, so the pattern changes
slowly enough to hear each shape. The selectors are appended host parameters (`ca_rule`, `ca_target`,
`ca_every`), off by default.

### Status

Usable. The user interface and pattern generation still need some more tightening up.