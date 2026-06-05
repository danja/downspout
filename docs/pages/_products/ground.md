---
title: Ground
order: 130
bundle: ground.vst3
kind: MIDI generator
role: Long-form bass generator
screenshot: /assets/plugins/ground.png
summary: Long-form bass generator with phrase roles, cadence, pedal, color, subject/answer arcs, and a guarded bass register lane.
---

## Functionality

Ground creates monophonic bass MIDI with a longer planning horizon than a simple
riff generator. It aims movement toward climbs, pedals, cadences, and releases
across phrases and sections, with color available for more chromatic or
Jazz-leaning tension. High Sequence with high Cadence gives it a
Fugue-friendly subject, dominant answer, pedal, and cadence form.

Ground is intended to drive bass instruments such as Basilico. Its phrase
planner can still create lift with Register and Register Arc, but generated
notes are folded back into a bass register lane after form generation, phrase
refresh, cell mutation, and loop variation. That keeps long-form motion from
turning into large octave jumps when the part is used as the low anchor.

## Controls To Watch

- `Register` chooses the base lane for the bass part.
- `Register Arc` controls how much phrase lift is allowed inside that lane.
- `Sequence` makes answer and release phrases reuse previous material.
- `Vary` mutates the form on loop boundaries while preserving the register
  guard.
