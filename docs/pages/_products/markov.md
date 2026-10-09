---
title: Markov
order: 210
bundle: markov.vst3
kind: MIDI generator
category: generative
role: Markov-chain melody generator
screenshot: /assets/plugins/markov.png
capabilities: [transport sync, MIDI input, MIDI output, persistent matrix state]
summary: A melody as a random walk through a visible transition matrix you can draw, pick a style for, or learn from a line you play.
---

> **Idea from [Subsequence](https://subsystem.co/subsequence)** — a Python generative sequencer that
> lists Markov generators among its building blocks. Markov's matrix editor, learning, second-order
> mode and code are original.

## Opinion

Not yet auditioned in a host. Expect melodies with a recognisable habit rather than a recognisable
tune: the matrix decides what the note after this one is likely to be, so the same few moves keep
coming back in different orders, and the picture of the matrix tells you why.

## Functionality

The melody is a random walk over the twelve pitch classes, counted from the **Root** note. The
matrix on screen has a row for the note just played and a column for each note that may follow it;
each cell's weight (0 to 8) says how likely that move is. Brightness shows the chance each move
actually has once the scale, anything learned and **Chaos** are applied, and the digit shows the
weight you set. Click a cell to cycle its weight. The outlined row is the one being used right now.

**Style** replaces the matrix with a ready-made habit: Stepwise (seconds, with some thirds),
Triadic (thirds and fifths), Fifths (fourths and fifths), Pentatonic, Blues (minor thirds and flat
fifths and sevenths), Chromatic (a semitone crawl), Tonic pull (stepwise, but leaning toward the
tonic, with the leading tone and fifth resolving home) and Uniform. **Clear matrix** zeroes it,
which plays any note in the scale, and **Randomise** draws a new one.

**Scale** (the 24 canonical scales of `docs/scales.md`) removes every note outside it, so any
matrix, drawn or learned, stays in key. An empty row plays any note in the scale. **Chaos** is an
exponent on the weights: below 50% it sharpens the favourites, above it flattens them toward
uniform, and 50% leaves them as drawn. **Order** 2 uses the two previous notes where the plugin has
learned anything about that pair, and the ordinary matrix otherwise.

Notes are placed in the register by taking the octave nearest the previous note, inside **Lowest
note** and **Octaves**. **Density** is the chance a step sounds, **Gate** its length, **Velocity**
its loudness (the first note of a phrase and notes on the beat are accented), and **Step grid**
the same twelve divisions as Sprout, including one bar.

### Phrases and repeatability

The walk restarts every **Phrase length** bars from the tonic, seeded by the **Seed** and the
phrase number. So every note is a pure function of its position: loops, a locate to the middle of a
song, a different block size and an offline render all play the same notes. The tests check that
starting playback at bar 3 plays exactly what a continuous run plays there.

### Learning from MIDI

Switch **Learn** on and play (or route) a single melodic line into the input. Every note-on
after the first is counted as a move from the previous note, relative to the Root, so octaves
count as the same note. **Learn channel** limits it to one channel (0 = all). Counts from
chords or several lines blur together, so feed it one line. The plugin keeps first-order counts
for every pair and second-order counts for every triple.

**Learned mix** blends the first-order counts into the matrix, row by row and only where a row has
heard something: 0% ignores them, 100% plays only what was heard where there is data. Order 2 uses
the second-order counts directly. **Clear learned** forgets everything. The matrix on screen, the
learned counts and the transitions-heard readout are all saved with the project.

### Conductor integration

Set **Conductor ch** (0 = off, 1-16) to the channel Conductor uses (default 16). CC 21 drives
Density, CC 22 Velocity, CC 23 Chaos, and CC 24 (value 127) re-rolls the melody: at the next bar
line the phrase count starts again with a new random draw. CC 20 (Scene) is not used. See
[MIDI Mapping](../../midi-mapping.md).

### Notes

- The editor does not move while a CC is driving a control (DPF has no DSP-to-panel path for that);
  the learned counts do update, a few times a second.
- Only the matrix goes from the editor to the plugin when you edit it, so a melody being learned
  at the same time is not overwritten.
- State: the model (matrix and learned counts) is saved as text; every setting is an ordinary
  host parameter. Output is MIDI only; route it to any instrument. Pair it with Retune and a
  per-channel-bend synth for microtonal Markov melodies.

### Status

Core DSP, deterministic tests (under ASan/UBSan), the VST3 target and the panel are complete.
Host validation in REAPER, and listening to which styles and orders sound good, are pending.
