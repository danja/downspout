---
title: Sprout
order: 209
bundle: sprout.vst3
kind: MIDI generator
category: generative
role: L-system melody generator
screenshot: /assets/plugins/sprout.png
capabilities: [transport sync, MIDI input, MIDI output]
summary: A grammar is rewritten generation by generation and played as a scale-quantised line, optionally growing one generation every few bars.
---

> **Idea from [Subsequence](https://subsystem.co/subsequence)** — a Python generative sequencer that
> offers L-systems among its algorithmic building blocks. Sprout's grammars, turtle
> reading and code are original.

## Opinion

Not yet auditioned in a host. Expect self-similar phrases: the repeating shapes of an
L-system are audible as motifs that return at different scales, which is different from
the randomness of most generators.

## Functionality

Choose one of seven grammars and a number of generations. Each generation rewrites every
symbol by the grammar's rules, and the final string is read like a turtle: `F` and `G`
play a note, `f` is a rest, `+` and `-` move the pitch up or down by **Step size** scale
degrees, `[` and `]` start and end a branch that returns to the pitch it left, and any
other letter only grows. Pitch folds back at **Range** degrees from the root, branches
are softer, and the first step of each pass is accented. **Probability** thins the notes
(seeded, so a pass repeats exactly).

The pattern is locked to the host transport and loops. With **Grow every** above zero the
pattern starts at generation 1 and gains a generation every N bars until it reaches
**Generations**, restarting at each change. Up to 16 generations are allowed, but expansions are limited to 131,072 steps, so each
grammar stops at the deepest generation that fits and Grow stops there too. Most generations each grammar reaches: Plant 7, Koch 6, Dragon 15, Sierpinski 9, Cantor 10, Levy 14, Tree 6. The
Pattern plot condenses long patterns to one column of pitch range per few pixels.

Scales use the canonical order in `docs/scales.md`.

### MIDI input

Sprout has one MIDI input, and everything on it is off until you choose it, so an unmodified
project behaves exactly as before.

**Pitch source: Held or Latched.** Instead of a scale, the turtle's degrees are mapped onto the notes
currently held on the input. Degree 0 is the lowest held note, and degrees are spread across the
held tones at about seven degrees per octave, so **Range** spans a similar number of octaves
whatever the chord size (a triad gets 0, 0, 0, 1, 1, 2, 2 for degrees 0 to 6, then the next
octave). With nothing held (or latched) the pattern rests; there is no fallback to the scale. **Root note**
and **Scale** are unused in this mode. **Note channel** limits which channel's notes count (0 =
all). The natural source is a chord generator in front of it (Harmonic Atlas, Cadence, ArpGen),
which makes Sprout's self-similar contours follow the harmony. Held notes are tracked from
note-on, note-off and note-on with velocity 0, and cleared by all-notes-off (CC 123 or 120).

*Held* plays the keys that are down now. *Latched* keeps the chord after the keys are released,
like an arpeggiator's latch: the first note pressed once every key is up replaces it, and notes
added while a key is still down join it. All-notes-off clears it, and switching back to Held drops
it. Use Latched when the chord source gates its notes, or to play a chord and let go.

**Drift CC channel** (CC set 1, off at 0). On that channel, CC 1 sets Probability, CC 2 Gate,
CC 3 Range and CC 4 Generations, each scaled across the parameter's range. These match Drift's
default lanes, so Drift into Sprout needs no setup.

**Conductor CC channel** (CC set 2, off at 0). On that channel, CC 21 (Density) sets
Probability, CC 22 (Energy) Velocity, CC 23 (Mutation) Seed, and CC 24 = 127 (Reset) restarts the
pattern at the next bar line. CC 20 (Scene) is not used. See `docs/midi-mapping.md`.

Notes on timing and behaviour:

- MIDI is applied at the start of each host block, so a note or CC takes effect on the first step
  that begins in that block or later.
- The CC sets write through to the parameter values, so the panel, automation and controller
  agree on what the plugin is using. The editor does not move while a CC is driving it (DPF has no
  DSP-to-panel path for that), and the last writer wins.
- **Generations** is now read once per bar, so a change from the panel, automation or CC 4 lands
  on a bar line instead of mid-bar. Constant settings are unaffected.
- The CC-driven result is repeatable on an offline render only if the same MIDI is replayed.
  A restart request that never reaches a bar line is dropped when the transport stops.

### Status

Core and wrapper are covered by deterministic tests. The grammars are built in: there is
no rule editor yet.
