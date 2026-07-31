# Mixgen

Mixgen is a transport-synchronized automatic producer for `t-mix`. It creates
eight repeatable gain lanes using random, quasi-random, or Euclidean logic and
emits them as MIDI CC 20-27. Route Mixgen's MIDI output to the track containing
T-Mix; channel 1 controls strip 1, through channel 8 controlling strip 8.

The main controls describe musical intent rather than MIDI plumbing: step rate,
pattern length, density, depth, accent variation, lane spread, and seed. The UI
previews every lane and shows the live gain being sent.

Mixgen passes stereo audio through unchanged, allowing it to sit on an existing
utility track as well as a dedicated MIDI-control track.
