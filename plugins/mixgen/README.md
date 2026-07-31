# Mixgen

Mixgen is a transport-synchronized automatic producer for the Transmission
Producer Control Bus. It creates
eight repeatable gain lanes using random, quasi-random, or Euclidean logic and
emits them as MIDI CC 20-27. Route Mixgen's MIDI output to the track containing
T-Mix; channel 1 controls strip 1, through channel 8 controlling strip 8.

The main controls describe musical intent rather than MIDI plumbing: step rate,
pattern length, density, depth, accent variation, lane spread, and seed. The UI
previews every lane and shows the live gain being sent.

Routing profiles select T-Mix only, effects only, or the Full bus. Four effect
macros default to Loopdelay time/feedback on CC 30/31 and Lightverb mix/space on
CC 32/33. Each macro can choose a source mix lane, destination CC, output range,
and inversion. CC 19 acquires producer ownership; stopping, disabling, or
moving channels releases it cleanly.

Mixgen passes stereo audio through unchanged, allowing it to sit on an existing
utility track as well as a dedicated MIDI-control track.

See [the Producer Control Bus specification](../../docs/producer-control-bus.md).
