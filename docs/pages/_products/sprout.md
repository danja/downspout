---
title: Sprout
order: 209
bundle: sprout.vst3
kind: MIDI generator
category: generative
role: L-system melody generator
screenshot: /assets/plugins/sprout.png
capabilities: [transport sync, MIDI output]
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

### Status

Core and wrapper are covered by deterministic tests. The grammars are built in: there is
no rule editor yet.
