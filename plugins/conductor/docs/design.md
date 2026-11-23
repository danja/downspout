# Conductor design

## Editable block form

The portable core holds a repeating list of up to 16 blocks (two rows of eight
in the panel). Block 1 is always at bar 0, and each block has one of seven
types: Null, Intro, Theme, Develop, Break, Reprise or Coda. `block_count` (1–16)
selects how many of the 16 stored blocks are active; the remaining slots are kept
so raising the count does not discard a user's work. The default eight-block
form reproduces the old Intro/Develop/Develop/Break/Develop/Reprise/Develop/Coda
arc; slots 9-16 default to Theme/Develop/Theme/Develop/Break/Develop/Reprise/Coda.

`block_count` and the 16 type parameters are ordinary host-automatable
parameters, not plugin state, so project save/recall and automation work without
a state contract. The eight new type parameters (`block_9_type` to
`block_16_type`, indices 31-38) were appended after `status_block` so older
projects keep their saved parameter indices. Status outputs expose the current block index, its type and
the bars remaining. A queued jump carries both a block index and an absolute
target bar, so it fires at the next bar even when the current block is longer.

Null is a rest: it releases any previous section note, sends the Null control
values (scene 1, all controls 0) and emits no new note.

## Fixed and weighted generation

Fixed mode cycles through the active blocks in order. Weighted mode treats the
active blocks as a palette: duplicate types share one weight, so repeating a
type in the editor does not accidentally multiply its probability. Weights come
from the seven shared type controls; Null and Theme default to zero. If every
active type has zero weight, all active types become equally likely. Seeded
choices and durations are derived from the absolute bar, so seek and offline
render rebuild the same schedule.

## Shared control protocol

`include/downspout/conductor_protocol.hpp` is the single source of truth for
block names, scene values and the density/energy/mutation/reset table used at
block boundaries. The five legacy scene values (Intro 0, Develop 32, Break 64,
Reprise 96, Coda 127) are retained; Theme is 16 and Null is 1. Ground, Harmonic
Atlas, Floozy and Melgen decode CC 20 through the same header, so the editor and
the receivers cannot drift apart. Other receivers ignore CC 20 and continue to
map CC 21–24 directly to their own masters.

## Notes and lifecycle

Each non-Null boundary releases the previous note on its original output
channel, then sends a new note and five CC commands. Release uses the channel
stored with the note, so changing Conductor's channel mid-block cannot strand a
note on the old channel. `activate()` and transport stop clear runtime state
only; they do not reset the edited form.
