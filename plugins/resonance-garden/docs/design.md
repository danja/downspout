# Resonance Garden design

Delay storage is allocated only during sample-rate preparation. Processing uses
fixed voices, bounded feedback, denormal suppression, finite-input recovery, and
soft-clipped output. MIDI note replacement is deterministic and held notes
decay cleanly after release.

The UI groups resonance, tuning, excitation, and output controls and visualizes
the bounded twelve-voice bank as a grid of the pitches each voice would take on
the internal scale. Active voices and output peak are read-only meters.

## Internal scale

`Internal scale` is an integer parameter, 0-22, in BassGen's order (`docs/scales.md`:
Major, Ionian, Minor, ... Bebop Minor; append only). The core keeps its own table, and the
tests hold an independent copy of every scale. The UI lists the names in a three-column
dropdown drawn in the plugin; `GenerativePanelUI` is shared and was not changed.

Voice `i` of the internal bank is `48 + root + degree(i)`, where degree `i` of the scale
climbs through the octaves (a five-note scale reaches the second octave at voice 6). The
voices are marked `internal`:

- while no MIDI note is held they sustain at a fixed level, so the bank keeps sounding
  and keeps answering the input;
- they follow the Internal root and scale live;
- a note-on takes a free voice, then an internal one, and only then steals round-robin;
  while a note is held the remaining internal voices fade as released voices do;
- once nothing is held, a released voice whose tail has died away goes back to its
  scale degree.

Before this the scale parameter was not read at all (the bank was a fixed pattern tuned once
at start-up that then faded away), and the saved scale index meant Major/Minor/Dorian/Penta
(0-3). Values 0 (Major) is unchanged and 2 now means Minor, but 1 and 3 become
Ionian and Harmonic Minor, so a project that had set 1 or 3 reopens on a different scale.
The old setting made no sound difference, so nothing audible changes for those projects.

Voice limit now runs 1-12 (it was 1-8); a saved limit keeps its value. Active voices
reports up to 12.
