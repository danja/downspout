# PadSeq design notes

## Layout and storage

PadSeq is a paged 8x8 editor. X is time, scanning left to right, and Y is note,
with note row 0 at the bottom hardware row. Each page shows eight steps; a pattern
has 1-32 steps (default 16), so up to four pages are used. Cells are stored
step-major: `cell(step, note) = step * 8 + note`. The 64 read-only status cells use
the visible layout instead: `status(note, stepColumn) = note * 8 + stepColumn`.

All 256 cells are ordinary boolean host parameters. Eight trigger parameters give
the UI the same note-preview action as the Launchpad side CCs, and the visible page
is mirrored by 64 read-only status parameters alongside status values for the step,
page, scale, running flag and note view. There is no `WANT_STATE`: DPF already
saves parameters by symbol, and there is no non-parameter data to serialise.

`activate()` and `sampleRateChanged()` clear only timing, queued note-offs and
LED caches. They never reset the pattern.

## Scales and notes

Scale ordinals follow `docs/scales.md` exactly: 0-32, including the generic
pentatonic at row 16. The table is append-only because hosts save the integer
ordinal. Row notes are `foldMidi(root + scaleStepAt(scale, note + noteView))`.
With **Note Map = GM Drums**, rows 0-7 are kick, snare, closed hat, open hat,
low tom, mid tom, high tom and crash.

## Clocks

Steps are sixteenth notes: four steps per beat.

- **Auto** uses host BBT when a valid transport exists, otherwise the free clock.
- **Host** counts absolute sixteenth steps from bar/beat. A start, forward jump
  or rewind rebases the pattern and restarts from step 0. Note-offs are queued in
  absolute-step space, so they survive a block boundary and are flushed on stop.
- **Free** accumulates samples from the tempo control. A tempo change clamps the
  accumulator so old samples cannot produce a burst of steps at frame 0.

Note-offs are queued in step space and flushed at the next boundary *after*
input has been consumed. A side CC or UI trigger just before a boundary therefore
releases at that boundary in the same block instead of one block late. With a
stopped host transport, manual triggers use a free-tempo accumulator for their
release only, so they cannot sustain indefinitely.

## Launchpad contract

Programmer mode is entered with `F0 00 20 29 02 0D 0E 01 F7`. Grid pads are
notes `11..18` on row 0 (bottom) through `81..88` on row 7. Side buttons are
CCs `19,29,39,49,59,69,79,89`. Top buttons are CCs `91..99`:

| CC | Action |
|---|---|
| 91 / 92 | Note view down / up |
| 93 / 94 | Previous / next page |
| 95 / 96 | Previous / next scale |
| 97 | Randomise (edit existing pattern, or seed a new one) |
| 98 | Clear pattern and panic |
| 99 | Unused logo button |

LED colours follow `docs/launchpad.md`. Active cells are green, the active cell
on the current step is white, inactive cells on the current step are dim, top
buttons are colour-coded by function and side buttons light when their note row
contains notes on the visible page. The UI's side triggers use the eight trigger
parameters, so a click has the same audible effect as a hardware side CC.

**LED Feedback is off by default**, but when enabled it is SysEx-only. The core
builds the complete grid/side/top colour state and emits one Novation lighting
message (`F0 00 20 29 02 0D 03 ... F7`); it never encodes colours as channel-1
note-ons or CCs. A synth sharing the MIDI output therefore cannot hear the scan
as held notes. Feedback stays opt-in to avoid unnecessary controller traffic.

**Clear / Panic** emits all-notes-off on the chosen channel, re-enters
programmer mode and sends a bulk SysEx that sets all 81 LEDs to colour 0. It then
holds every LED dark until an intentional edit, note trigger, transport start or
LED-feedback re-enable, so a periodic refresh cannot re-light stale buttons.

## Randomisation

The seed parameter initialises a deterministic xorshift generator. Randomise
edits an existing pattern by thinning notes and adding a few new ones, so an
idea stays recognisable; an empty pattern gets a fresh sparse one. Rerolling
uses the running generator state rather than restarting from the seed, so
consecutive presses differ. The seed can be changed to jump to a different
sequence.

## Testing

`plugins/padseq/tests/padseq_core_tests.cpp` checks the canonical scale table,
grid mapping, horizontal scan and status layout, note folding, all controls, free
and host timing, jumps/rewind, late-block note-offs, hardware and UI manual
triggers, Launchpad lighting SysEx, no note/CC LED artifacts, hard-clear reset,
randomisation, panic and lifecycle state retention. Host save/reopen was validated in REAPER; realtime
transport and Launchpad LED messages still need a reachable host transport and
hardware.
