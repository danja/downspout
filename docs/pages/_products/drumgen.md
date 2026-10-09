---
title: DrumGen
order: 70
bundle: drumgen.vst3
kind: MIDI drum generator
category: generative
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

### Conductor integration

DrumGen accepts MIDI input for Conductor CC control. Set **Conductor Ch**
(0 = off, 1–16) to the channel Conductor uses for its output (default 16).
Conductor CC 21 drives density, CC 22 drives variation, CC 23 drives mutation
rate, and CC 24 (value 127) triggers a new pattern on each section boundary.
See [MIDI Mapping](../../midi-mapping.md).

### Automaton layer

An optional cellular-automaton layer evolves chosen lanes so a pattern keeps changing without
being regenerated. Pick an **Automaton rule** (Off, or Wolfram rule 30, 90, 110, 150, 18, 54, 60,
22, 105 or 126) and what it **plays on**: Hats, Percussion (bash, cowbell, clave), Toms, all three
together, or every lane.

The pattern as generated is generation 0 and plays on the first pass. On each later pass a targeted
lane plays the next row of a one-dimensional automaton (wrapping at the pattern length) seeded from
that lane's own hits, so hats thin out, fill in and shift in ways that are structured rather than
random. New hits take the lane's average velocity and surviving hits keep theirs. A lane with no hits
stays silent. After 64 generations a lane returns to the pattern. Kick, clap, snare and crash are
never touched unless you choose All lanes.

The pass number comes from the playback position, so loops, restarts and offline renders agree, and
the stored pattern is never altered (New, Mutate and Vary all still work on it). Rule 90 gives
symmetric, self-similar shapes, 30 and 110 are busier and less predictable, and 150 and 105 are
dense. Both selectors are appended host parameters (`ca_rule`, `ca_target`), off by default.

### Status

Usable. The user interface and pattern generation still need some more tightening up.