# ghost — design notes

INBOX brief: *"ghost will listen to incoming audio and output midi. It will
determine appropriate places, in time with the transport BBT, to insert midi
note events corresponding to ghost drum beats (midi channel 10) or notes (midi
channel selectable). It will have 4 controls (TBD) that correspond to the CC
messages sent by plugins/drift."*

## Structure

```
include/ghost_core.hpp   Parameters, EngineState, processBlock, text state
src/ghost_core.cpp       envelope onset detector, 16th-slot quantiser, fills
src/dpf/GhostPlugin.cpp  state-driven wrapper, Drift CC 1-4 overrides
src/dpf/GhostUI.cpp      two-column NanoVG panel (parameters + CC routing)
```

The core depends on `plugins/generative-common/include/generative_common.hpp`
for `Transport`, `MidiBlock`, `absoluteQuarter`, `isDiscontinuity` and the
deterministic `randomUnit`/`randomInt` streams — the same header oracle uses.

## The 4 TBD controls

Resolved as Sensitivity, Density, Velocity, Drag, mapped 1:1 to Drift's default
lane CCs 1–4 (the chipper/magneto convention). Sensitivity sets the onset flux
threshold `thr = 0.30*(1-sens)+0.003`; Density is the per-off-16th fill
probability; Velocity is the accent velocity (fills sound at ~45–75% of it, the
ghost dynamic); Drag is ghost lateness as a fraction of a 16th slot.

## Assumptions

**Fixed 16th grid.** No Division control in v1 — ghost vocabulary lives on
16ths, and the four available controls were spent on feel parameters. A Division
(8th/16th/32nd/triplet) control is the obvious first extension.

**Onset accents quantise forward.** An onset schedules into the *next* 16th
slot plus drag rather than firing immediately, so accents sit in the grid even
when the transient lands between slots. If the quantised frame falls outside
the current block the accent is dropped (the following block's fill logic
covers the energy).

**Fills need audio present.** Off-16th candidates only fire when the envelope
exceeds 0.004, so silence in means silence out even at Density 1.

**Drums voices.** Slot % 8 == 0 gives kick 36, everything else ghost snare 38 —
kick ghosts on the quarter, snare ghosts off it. Notes mode walks
`{0,3,5,7,10,12,15}` above Base Note via the seeded stream.

**Running transport only.** Like drift (unlike oracle's always-on meters),
emission requires `valid && playing`; the envelope still tracks while stopped
so the first ghost after play lands correctly. Pending note-offs flush at the
top of the next block regardless.

**Routing CCs are consumed.** CC 1–4 on the CC channel never reach the output;
everything else is forwarded when Pass MIDI is on.

## Visual acceptance

Panel follows the chipper two-column layout with shared look-and-feel tokens.
The catalogue screenshot is captured and reviewed (mode label renamed from
"Mode Dr/Nt" after first review); recapture with
`scripts/capture-plugin-screenshots.sh ghost` after any UI change. Live onset
metering is deferred — the UI is state-editors only in v1.
