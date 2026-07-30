# Harmonic Atlas design

The portable core derives every chord from absolute host position, parameters,
and seed. No hidden PRNG stream is required for state restoration. Incoming
note-ons only update an optional root pitch class. One MIDI bus carries roots,
chord tones, and optional scale color; output is capped at seven simultaneous
notes. Stop, rewind, and seek release the active chord before resynchronizing.
