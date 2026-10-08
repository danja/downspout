---
title: Retune
order: 208
bundle: retune.vst3
kind: MIDI effect
category: midi
role: Scala microtuning
screenshot: /assets/plugins/retune.png
capabilities: [MIDI note and CC input, MIDI output]
summary: Retunes any MIDI note stream to a Scala .scl scale using one pitch bend and one channel per note, so ordinary generators can drive microtonal synths.
---

> **Idea from [Subsequence](https://subsystem.co/subsequence)** — a Python generative sequencer that
> supports microtonal output from Scala files and N-TET via per-note pitch bend.
> Retune applies the same approach to any Downspout generator. The code is original.

## Opinion

Not yet auditioned in a host. The approach is the standard MPE-style one, so it
will only help with synths that follow pitch bend per channel.

## Functionality

Load a Scala `.scl` file (cents and ratio lines are both accepted) and set the root
note, which plays the scale's 1/1. Each incoming note is mapped through the scale,
rounded to the nearest 12-tone key, and sent on its own output channel (2 to 16)
after a pitch bend that corrects the difference. The scale map shows one period
with the 12-tone keys above the scale degrees. Set the receiving synth's pitch bend
range to match **Bend range** (default 2 semitones).

Control change and program change are sent to every output channel, so sustain, mod
wheel and volume still work. Polyphonic aftertouch follows its note. Incoming pitch
bend and channel aftertouch are dropped, because they would undo the tuning. When
fifteen notes are held, the oldest is stolen. With no file loaded the plugin plays
12-tone equal temperament, which leaves notes unchanged apart from the channel move.

### Status

Core and wrapper are covered by deterministic tests; the keyboard mapping (`.kbm`)
file is not supported, and the scale file path rather than its contents is saved
with the session.
