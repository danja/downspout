# Mosaic

Mosaic is a four-slot WAV sampler with MIDI and autonomous triggering. Seeded
slice selection, pitch variation, reverse probability, stereo spread, and
transient-safe envelopes produce repeatable variations.

## Pitch bend

Mosaic follows MIDI pitch bend **per channel**, so it can be driven by a per-channel
retuner such as Retune (one note per channel, each with its own bend). Each slice
voice follows the bend of the channel its note arrived on; bends on other channels
do not touch it. The range is the **Pitch bend range** control (1-24 semitones,
default 2, the same for every channel; set it to match Retune's Bend range). Bend is
applied at block resolution to slices already sounding.

The note only sets the pitch when **Pitch range** is 0. At any other setting each
slice gets a random transpose within that range and the note is ignored (the default
is 7), so set Pitch range to 0 to hear bend.
