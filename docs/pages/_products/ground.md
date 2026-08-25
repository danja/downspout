---
title: Ground
order: 130
bundle: ground.vst3
kind: MIDI generator
role: Long-form bass generator
screenshot: /assets/plugins/ground.png
summary: Long-form bass generator with Grounded, Dub, Jazz, and other phrase styles plus a guarded bass register lane.
---

## Opinion

The UI probably needs some more work, I don't find it very intuitive (can't even tell if there are bugs in its sequencing). I tend to place it and forget about it. More integration with Conductor and the other generative plugins would be good.
  
## Functionality

Ground creates monophonic bass MIDI with a longer planning horizon than a simple
riff generator. It aims movement toward climbs, pedals, cadences, and releases
across phrases and sections, with color available for more chromatic or
Jazz-leaning tension. High Sequence with high Cadence gives it a
Fugue-friendly subject, dominant answer, pedal, and cadence form.

The Style selector includes Grounded, Ostinato, March, Pulse, Drone, Climb, Dub,
and Jazz. Dub keeps the part sparse, low, and heavy with off-beat pickups. Jazz
leans into walking-bass quarter motion and approach tones while still following
the long-form phrase plan.

Ground is intended to drive bass instruments such as Basilico. Its phrase
planner can still create lift with Register and Register Arc, but generated
notes are folded back into a bass register lane after form generation, phrase
refresh, cell mutation, and loop variation. That keeps long-form motion from
turning into large octave jumps when the part is used as the low anchor.

### Conductor integration

Ground accepts MIDI input for Conductor CC control. Set **Conductor Ch**
(0 = off, 1–16) to the channel Conductor uses for its output (default 16).
Conductor CC 21 drives density, CC 22 drives motion, CC 23 drives mutation rate,
and CC 24 (value 127) triggers a new form. CC 20 (Scene) additionally maps the
arrangement position to arc tension and writes a phrase role override for the
current phrase, so Ground's peak aligns with the arrangement's energy peak and
phrase roles track the song's section vocabulary.
See [MIDI Mapping](../../midi-mapping.md).

### Status

Under evaluation. It can generate useful long-form patterns but the user interface isn't very intuitive.

## Controls To Watch

- `Register` chooses the base lane for the bass part.
- `Register Arc` controls how much phrase lift is allowed inside that lane.
- `Style` chooses the local rhythmic attitude, including explicit Dub and Jazz
  behavior.
- `Sequence` makes answer and release phrases reuse previous material.
- `Vary` mutates the form on loop boundaries while preserving the register
  guard.
