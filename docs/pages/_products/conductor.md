---
title: Conductor
order: 111
bundle: conductor.vst3
kind: MIDI generator
role: Long-form structure
screenshot: /assets/plugins/conductor.png
capabilities: [MIDI output, host transport, scene CC, deterministic form, generator control]
summary: Bar-aligned intro, development, break, reprise, and coda commands for autonomous patches.
---

## Opinion

New, I've only just started experimenting.

## Functionality

Conductor emits section notes and scene, density, energy, mutation, and reset
CC values on each section boundary. Forms can follow a fixed arc or a seeded
weighted section graph.

Route its MIDI output to BassGen and/or DrumGen and set their **Conductor Ch**
parameter to match Conductor's output channel to let it automatically reshape
density, accent, variation, and pattern resets as the arrangement progresses.
See [MIDI Mapping](../../midi-mapping.md).
