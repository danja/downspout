---
title: Conductor
order: 111
bundle: conductor.vst3
kind: MIDI generator
category: generative
group: generators
role: Long-form structure
screenshot: /assets/plugins/conductor.png
capabilities:
  [MIDI output, host transport, scene CC, editable block form, deterministic form, generator control]
summary: Bar-aligned arrangement blocks (Null, Intro, Theme, Develop, Break, Reprise, Coda) with shared scene CC values for downstream generators.
---

## Opinion

Long-form arrangement is easier to trust when the form is visible and directly
editable. The block strip shows the active block, queued jump and each block's
type; fixed and weighted modes can be compared without changing routing.

## Functionality

Conductor repeats up to eight blocks chosen from Null, Intro, Theme, Develop,
Break, Reprise and Coda. Fixed mode plays the edited order; weighted mode picks
from the active block types using the seven weight controls. A queued jump takes
effect at the next bar, and scene/density/energy/mutation/reset CC values follow
the shared Conductor protocol. Null is a rest that emits no section note.

Route its MIDI output to BassGen, DrumGen, Ground, Melgen, Floozy, Harmonic Atlas
and/or other generators with a **Conductor Ch** parameter set to match
Conductor's output channel (default 16). See [MIDI Mapping](../../midi-mapping.md).
