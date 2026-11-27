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
just before a step boundary. LED feedback defaults on and follows Plank's
channel-1 note/CC LED path; generated notes default to channel 4, so an
instrument sharing the output must listen on channel 4 or filter channels 1-3.
**Clear / Panic** blacks out all LEDs over both note/CC and SysEx and keeps them
dark until the next edit or run.

Clock mode is Auto, Host or Free. Host mode counts absolute sixteenth steps and
restarts cleanly on start, jump and rewind; free mode follows the internal
tempo. All cells are ordinary host parameters, so projects save the pattern
without plugin state.

### Hardware

Route the Launchpad MIDI input into the PadSeq track and that track's MIDI
output back to the controller. PadSeq sends the programmer-mode handshake
automatically; LED Feedback is on by default and uses Plank-style channel-1
note/CC LEDs, with a bulk SysEx for hard clear.

### Status

Deterministic core tests cover the scale table, grid mapping, every control,
host and free clocks, launches, jumps, rewind, Launchpad messages, randomise,
panic and host lifecycle. The VST3 target builds; real Launchpad validation and
a listening check remain.
