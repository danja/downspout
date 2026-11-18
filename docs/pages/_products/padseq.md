---
title: PadSeq
order: 250
bundle: padseq.vst3
kind: MIDI generator
category: launchpad
group: rhythm
role: Paged step sequencer
screenshot: /assets/plugins/padseq.png
summary: Paged 8x8 MIDI step sequencer for a Launchpad, with time columns scanning left to right, note rows, 1-32 steps, canonical scales and GM drum mapping.
---

## Opinion

A Launchpad is a natural step sequencer: eight columns of time and eight rows of
notes fit the hardware exactly. PadSeq scans left to right across each page, so a
16-step pattern needs one button press instead of a scrolling editor.

## Functionality

PadSeq edits a 1-32 step pattern (default 16) on an 8x8 grid. Columns are
successive sixteenth-note steps and rows are notes, with row 0 at the bottom.
The current scale comes from the canonical 33-row `docs/scales.md` table, and
**Note Map** can replace scale degrees with General MIDI drums. **Root** and
**Note View** shift the pitched rows; **Randomise** edits an existing pattern or
seeds a new one, and **Clear / Panic** empties it and silences all notes.

The top Launchpad buttons mirror the UI strip: note view, page, scale,
randomise and clear. The eight side buttons and their UI equivalents trigger
note rows with note-offs timed to the next step, including presses that arrive
just before a step boundary. LED feedback is off by default; when enabled it
uses Launchpad lighting SysEx only, so a synth sharing the output cannot hear
the scan as notes. **Clear / Panic** blacks out all LEDs and keeps them dark
until the next edit or run. Generated MIDI uses channel 4 by default.

Clock mode is Auto, Host or Free. Host mode counts absolute sixteenth steps and
restarts cleanly on start, jump and rewind; free mode follows the internal
tempo. All cells are ordinary host parameters, so projects save the pattern
without plugin state.

### Status

Deterministic core tests cover the scale table, grid mapping, every control,
host and free clocks, launches, jumps, rewind, Launchpad messages, randomise,
panic and host lifecycle. The VST3 target builds; host validation and a
listening check remain.
