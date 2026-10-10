# Conductor → Generator MIDI Mapping

## Overview

Conductor emits MIDI CCs on each section transition to communicate song structure.
BassGen, DrumGen, and Ground can listen on a configurable channel and apply those CCs
to live controls, so generator behaviour changes automatically as the arrangement
progresses.

## Conductor output protocol

All messages are sent on Conductor's configurable output channel (default: **ch 16**).

| CC (default) | Name    | Section values (Intro / Devel / Break / Reprise / Coda) |
|:---:|---------|----------------------------------------------------------|
| 20 | Scene   | 0 / 32 / 64 / 96 / 127                                  |
| 21 | Density | 30 / 74 / 18 / 62 / 20                                  |
| 22 | Energy  | 28 / 68 / 20 / 58 / 42                                  |
| 23 | Mutation| 12 / 54 / 38 / 32 / 8                                   |
| 24 | Reset   | 127 / 0 / 0 / 0 / 127                                   |

CCs are sent once per bar boundary when a new section starts.  Reset=127 marks
section boundaries where a new pattern should be generated (Intro and Coda).

## BassGen mapping

BassGen adds one new parameter:

| Parameter | Symbol | Range | Default | Purpose |
|-----------|--------|-------|---------|---------|
| Conductor Ch | `conductor_ch` | 0–16 | 0 (off) | MIDI channel to receive Conductor CCs; 0 = disabled |

CC→control mapping (all values scaled linearly from 0–127 → 0.0–1.0):

| CC | Conductor name | BassGen control | Notes |
|----|---------------|-----------------|-------|
| 21 | Density | `density` | Direct note-density override |
| 22 | Energy | `accent` | Higher energy → stronger accents |
| 23 | Mutation | `vary` | Higher mutation → more auto-variation |
| 24 | Reset | `actionNew` (trigger) | Fires when CC value = 127; generates a fresh pattern |

CC 20 (Scene) is not mapped — BassGen's genre/scale are considered arrangement-level
decisions that the composer sets by hand.

## DrumGen mapping

DrumGen gains a MIDI input port and one new parameter:

| Parameter | Symbol | Range | Default | Purpose |
|-----------|--------|-------|---------|---------|
| Conductor Ch | `conductor_ch` | 0–16 | 0 (off) | MIDI channel to receive Conductor CCs; 0 = disabled |

CC→control mapping:

| CC | Conductor name | DrumGen control | Notes |
|----|---------------|-----------------|-------|
| 21 | Density | `density` | Overall hit density |
| 22 | Energy | `variation` | Higher energy → busier, more varied patterns |
| 23 | Mutation | `vary` | Auto-mutation rate |
| 24 | Reset | `actionNew` (trigger) | Fires when CC value = 127; generates a fresh pattern |

CC 20 (Scene) is not mapped — genre selection remains a manual choice.

## Ground mapping

Ground gains a MIDI input port and one new parameter:

| Parameter | Symbol | Range | Default | Purpose |
|-----------|--------|-------|---------|---------|
| Conductor Ch | `conductor_ch` | 0–16 | 0 (off) | MIDI channel to receive Conductor CCs; 0 = disabled |

CC→control mapping:

| CC | Conductor name | Ground control | Notes |
|----|---------------|----------------|-------|
| 20 | Scene | `tension` + phrase role override | Tier 2: Scene 0–127 → tension 0.0–1.0. Tier 3: also writes a phrase role override for the current phrase (see table below) |
| 21 | Density | `density` | Overall note density |
| 22 | Energy | `motion` | Higher energy → more melodic movement |
| 23 | Mutation | `vary` | Auto-mutation rate |
| 24 | Reset | `actionNewForm` (trigger) | Fires when CC value = 127; regenerates the whole arc |

### CC 20 scene → phrase role mapping (Tier 3)

| CC 20 value | Conductor section | Ground phrase role |
|:-----------:|-------------------|-------------------|
| 0–16 | Intro | Statement |
| 17–48 | Develop | Climb |
| 49–80 | Break | Breakdown |
| 81–112 | Reprise | Answer |
| 113–127 | Coda | Cadence |

The role is written as an override for the phrase currently playing.  Only the active
phrase is overridden; earlier phrases retain their assigned roles.

## DAW routing

1. Route Conductor MIDI output into BassGen, DrumGen, and/or Ground MIDI input.
2. Set **Conductor Ch** on each generator to match Conductor's output channel (default 16).
3. Leave generator Ch at its output channel (BassGen default 1, DrumGen default 10, Ground default 1).

The CC channel matching is done in the DPF wrapper before `processBlock`, so it is
RT-safe and requires no changes to the portable cores.

## CC number assumptions

This implementation hardcodes Conductor's default CC numbers (20–24).  If the user
changes Conductor's CC assignments, the generators will not follow.  Future work could
expose CC number parameters on the generator side.

## Bubbles mapping

Bubbles adds one new parameter for Conductor reception:

| Parameter | Symbol | Range | Default | Purpose |
|-----------|--------|-------|---------|---------|
| Conductor Ch | `conductor_ch` | 0–16 | 0 (off) | MIDI channel to receive Conductor CCs; 0 = disabled |

CC→control mapping:

| CC | Conductor name | Bubbles control | Notes |
|----|---------------|-----------------|-------|
| 21 | Density | `density` | Overall bubble/drip spawn rate |
| 22 | Energy | `flow` | Noise flow and wave amplitude |
| 23 | Mutation | `randomness` | Stochastic pitch jitter |
| 24 | Reset | spawn burst (trigger) | Fires when CC value = 127; immediately triggers a burst of bubble/drip events |

CC 20 (Scene) is not mapped — mode selection is a manual arrangement decision.

## Lifeform mapping

Lifeform gains one parameter, **Conductor Ch** (`conductor_ch`, 0-16, default 0 = off). The control sits in the
bottom button row of the panel (click to cycle Off, then channels 1-16).

| CC | Conductor name | Lifeform control | Notes |
|----|---------------|------------------|-------|
| 21 | Density | `density` | How full a random pattern is |
| 22 | Energy | `velocity` | Note velocity |
| 23 | Mutation | `mutation` | Chance cells flip each generation |
| 24 | Reset | randomise (trigger) | Fires when CC value = 127; draws a fresh random pattern at the current density |

CC 20 (Scene) is not mapped. The Conductor CCs still pass through when **Pass Input** is on, so a Conductor feeding
Lifeform can keep feeding what follows. Launchpad button CCs (91-99 and 19-89) are unaffected.

## Luma mapping

Luma gains one parameter, **Conductor Ch** (`conductor_ch`, 0-16, default 0 = off), the **Cond** button at the right
of the bottom button row.

| CC | Conductor name | Luma control | Notes |
|----|---------------|--------------|-------|
| 21 | Density | `density` | How full a scattered pattern is |
| 22 | Energy | `energy` | Note energy |
| 24 | Reset | scatter (trigger) | Fires when CC value = 127 |

CC 20 (Scene) and CC 23 (Mutation) are not mapped. The CCs still pass through when **Pass** is on. Launchpad button
CCs are unaffected.

## Polymeter mapping

Polymeter gains a MIDI input and three parameters: **Conductor ch** (`conductor_ch`, 0-16, default 0 = off) and two
master controls the CCs drive, **Density** (`density`, 0-1, default 1) and **Energy** (`energy`, 0-1, default 1). Both
masters default to "no change", so a project that does not use them plays exactly as before.

| CC | Conductor name | Polymeter control | Notes |
|----|---------------|-------------------|-------|
| 21 | Density | `density` | Multiplies every lane's Probability |
| 22 | Energy | `energy` | Scales every lane's velocity (100% = as set, 0% = 40% of it) |
| 23 | Mutation | `seed` | Spans the whole seed range, so it re-rolls the probability choices and the automaton rows |
| 24 | Reset | restart (trigger) | Fires when CC value = 127; every lane restarts from its first step at the next bar line |

CC 20 (Scene) is not mapped. The restart waits for a bar line, so a restart sent mid-bar takes effect when the bar
changes. A transport stop or jump discards a pending restart.

## Xoxolo mapping

Xoxolo gains a MIDI input and three parameters: **Conductor Ch** (`conductor_ch`, 0-16, default 0 = off), **Density**
(`density`, shown as "Hit density") and **Energy** (`energy`), both 0-1 with a default of 1 = no change.

| CC | Conductor name | Xoxolo control | Notes |
|----|---------------|----------------|-------|
| 21 | Density | `density` | Probability that a programmed hit plays (seeded by step and lane, so repeatable) |
| 22 | Energy | `energy` | Velocity: 100% plays the programmed 100, 0% plays 40 |
| 23 | Mutation | evolve generation | Adds 0-63 generations to the Evolve layer (see the Evolve controls); has no effect on lanes that are not marked or while the rule is off |
| 24 | Reset | restart (trigger) | Fires when CC value = 127; the pattern and the evolve generation are counted from the next bar line |

CC 20 (Scene) is not mapped. The mutation shift is performance state and is not saved with the project.

## Counterpointer mapping

Counterpointer gains one parameter, **Conductor** (`conductor_ch`, 0-16, default 0 = off).

| CC | Conductor name | Counterpointer control | Notes |
|----|---------------|------------------------|-------|
| 21 | Density | `density` | How often the counter-line plays |
| 22 | Energy | `embellish` | Ornamentation around the line |
| 23 | Mutation | `short_random` | Short-range randomness |
| 24 | Reset | relearn (trigger) | Fires when CC value = 127; learns the incoming line afresh |

CC 20 (Scene) is not mapped.

## Cadence mapping

Cadence gains one parameter, **Conductor Ch** (`conductor_ch`, 0-16, default 0 = off).

| CC | Conductor name | Cadence control | Notes |
|----|---------------|-----------------|-------|
| 21 | Density | `complexity` | Harmonic richness |
| 22 | Energy | `movement` | How much the harmony moves |
| 23 | Mutation | `vary` | Variation |
| 24 | Reset | relearn (trigger) | Fires when CC value = 127; learns the incoming harmony afresh |

CC 20 (Scene) is not mapped.

## Markov mapping

Markov has one parameter for Conductor reception, **Conductor ch** (`conductor_ch`, 0-16, default 0 = off).

| CC | Conductor name | Markov control | Notes |
|----|---------------|----------------|-------|
| 21 | Density | `density` | Chance a step sounds |
| 22 | Energy | `velocity` | Base velocity, 1-127 |
| 23 | Mutation | `chaos` | 0 sharpens the favourites, 1 flattens toward uniform |
| 24 | Reset | re-roll (trigger) | Fires when CC value = 127; at the next bar line the phrase count starts again with a new random draw |

CC 20 (Scene) is not mapped. A pending re-roll is dropped when the transport stops or jumps.

## Magneto mapping

Magneto is not a Conductor receiver. It takes ordinary controller and note
messages on the channel chosen by its **MIDI in** parameter (`midi_ch`;
0 = off, 1–16 = that channel only, 17 = all).

Notes set crankshaft speed rather than gating a voice. The note is transposed
down two octaves first, then read as a frequency and converted with
`rpm = 120 x hz`, so C1 idles near 980 rpm, C2 sits near 1960, and C4 reaches
the 9000 rpm limit. Velocity sets Throttle on the same scale as CC 1. With four cylinders the firing rate lands on the pitch
actually played, since firing rate = cylinders x engine cycle rate. Note-off is
ignored: the engine holds the last speed and load it was given and Inertia does
the rest. Under *Host Sync* a note still sets Throttle but not RPM, which Sync
owns.

| CC | Magneto control | Notes |
|----|-----------------|-------|
| 1 | Throttle | Mod wheel. Ignition energy and block vibration |
| 2 | RPM | Full 400–9000 rpm range |
| 3 | Silencing | Muffler action, 0 straight through to 1 silent |
| 4 | Growl | Cycle asymmetry |
| 5 | Straight pipe | 0.20–4.00 m |
| 6 | Turbulence | Aspiration noise |
| 7 | Output | Channel volume |
| 11 | Throttle | Expression, alias of CC 1 |

CC 1–4 are deliberately the four most audible controls because those are the
numbers [Drift](../plugins/drift/) emits from its four lanes by default, so
routing Drift into Magneto modulates the engine without configuration. Retarget
Drift's lane CCs to 5, 6 or 7 to reach the rest.

A controller moves the engine but not the on-screen slider — DPF has no
DSP-side path back to the panel. The tachometer reads the processor's live
speed, so it stays truthful under MIDI, automation or tempo control.

## Behaviour when disabled

When **Conductor Ch** = 0, all CC scanning is skipped and existing Note On follow/dodge
behaviour (BassGen) or fully generative behaviour (DrumGen) is unchanged. The same holds for Lifeform, Polymeter and
Xoxolo: with the channel off, and the masters at their defaults, output is bit-for-bit what it was before.
