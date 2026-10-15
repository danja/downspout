# Beards

Beards is a granular texture processor, a port of the Mutable Instruments Beads
grain engine (MIT, Emilie Gillet). Stereo input is recorded into a 4 second buffer and
replayed by up to 32 overlapping grains.

- **Grains:** Position (how far back), Size (grain length; below a third plays
  backwards), Shape (envelope), Pitch (-24 to +24 st), Density (grain rate), and four
  scatter amounts that randomise Position, Size, Shape and Pitch per grain.
- **Output:** Feedback, Dry/Wet, Reverb, Freeze, a Grain button, and Quality (Digital,
  Vintage 12 bit, Tape 8 bit mu-law).
- **Control:** Drift on **MIDI in** (CC 1 Position, 2 Size, 3 Density, 4 Dry/Wet by
  default, so no setup; 5-12 reach the rest; CC 64 freezes) and Conductor on its own
  channel (CC 21 Density, 22 Energy to Dry/Wet, 23 Mutation to scatter, 24 Reset fires a
  grain). Both are off by default. See `docs/midi-mapping.md`.

Density between 48% and 52% plays no automatic grains (use the Grain button or
Conductor Reset). Feedback and Reverb can build up; trim output. See `docs/design.md`
for what was and was not carried over from Beads.

Tests: `ctest -R beards`.
