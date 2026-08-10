---
title: DrumGen
order: 70
bundle: drumgen.vst3
kind: MIDI drum generator
role: Drum pattern generator
screenshot: /assets/plugins/drumgen.png
capabilities: [MIDI output, MIDI input, host transport, pattern variation, fills, conductor control]
summary: Transport-aware MIDI drum generator with fills, style modes, Breakbeat/Jazz/Fugue genres, and sparse pulse options.
---

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

### Status

Usable. The user interface and pattern generation still need some more tightening up.