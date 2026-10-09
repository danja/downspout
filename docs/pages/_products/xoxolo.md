---
title: Xoxolo
order: 75
bundle: xoxolo.vst3
kind: MIDI generator
category: generative
role: Drum pattern editor
screenshot: /assets/plugins/xoxolo.png
summary: Simple x0x-style MIDI drum pattern editor with note-map presets, per-lane preview, and selectable 8-32 step patterns, and an optional cellular-automaton Evolve layer per lane.
---

## Opinion

It's minimal, could do with support for longer patterns. But for what it is, it works well.

## Functionality

Xoxolo is a direct MIDI drum pattern editor for `drumkit.vst3` and other drum
instruments. It presents a small step grid, loops from host transport, and keeps
all pattern steps visible.

The first version uses 11 Downspout lanes or 29 AVL-Drumkits lanes, selectable
8-32 step patterns, channel 10 by default, and explicit text state for cells,
lane notes, and the selected preset.

### Evolve (cellular automaton)

Any lane can be set to evolve. Click the **~** button at the right of a lane to mark it, and choose
an **Evolve rule** (Off, or Wolfram rule 30, 90, 110, 150, 18, 54, 60, 22, 105 or 126) in the panel.
The pattern you programmed is generation 0 and plays on the first pass; each later generation
replaces that lane's hits with the next row of a one-dimensional automaton (wrapping at the pattern
length) seeded from the lane. **Generation lasts** sets how many passes through the pattern each
generation is held (1 to 8). After 64 generations the lane returns to what you programmed, and a
lane with no hits stays silent.

Only what is played changes: the grid keeps your programmed pattern, so you can edit it as normal and
turn Evolve off to hear it again. Unmarked lanes always play their grid, so a kick and snare can stay
fixed while hats and percussion evolve. The pass number comes from the playback position, so loops,
restarts and offline renders agree. A marked lane is shown lit while a rule is chosen and dim when the
rule is off. Marked lanes are saved with the pattern (an `evolve` line per lane, written only when
used); the rule and period are host parameters (`ca_rule`, `ca_every`).

### Conductor integration

Xoxolo has a MIDI input for Conductor CCs only. Set **Conductor** to the channel Conductor uses (default 16). CC 21
drives **Hit density**, the probability that a programmed hit plays (seeded by step and lane, so a render is
repeatable); CC 22 drives **Energy**, which plays the programmed velocity of 100 at full and 40 at zero; CC 23
(Mutation) adds 0 to 63 generations to the Evolve layer, so a building arrangement pushes marked lanes further from
the grid; and CC 24 (value 127) restarts the pattern and the evolve generation at the next bar line. Hit density and
Energy are also host parameters with sliders in the panel, both at 100% by default. CC 20 (Scene) is not used, and
the mutation shift is performance state, not saved. See [MIDI Mapping](../../midi-mapping.md).

### Status

Close to the required functionality, real-world testing in progress.