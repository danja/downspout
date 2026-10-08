# Polymeter

Polymeter generates four Euclidean MIDI lanes on one event output. Independent
lengths, pulses, rotations, probability, ratchets, accents, phase drift, notes,
and channels create long repeatable coprime cycles.

Each lane also has an **Automaton rule** (0-255). At 0 the lane is Euclidean as
before. At 1-255 the lane plays a one-dimensional cellular automaton (Wolfram rule
numbering, wrapping edges): its starting row has each cell alive with probability
Pulses / Length (seeded), and the automaton advances one generation per cycle of the
lane, returning to the starting row after 64 generations. Rotation, probability,
ratchets and phase drift still apply. The idea comes from Subsequence's cellular-automaton
building block (see `docs/subsequent.md`). The rule parameters are appended after the
original ones, so saved projects and automation keep their meaning.
