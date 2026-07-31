# Harmonic Atlas design

The portable core derives every chord from absolute host position, parameters,
and seed. No hidden PRNG stream is required for state restoration. Incoming
note-ons only update an optional root pitch class. One MIDI bus carries roots,
chord tones, and optional scale color; output is capped at seven simultaneous
notes. Stop, rewind, and seek release the active chord before resynchronizing.

The UI groups harmony, voicing, input/routing, and processor state. Movement and
voicing enums use musical names; pitch classes use note names; the live root
and progression step drive a compact keyboard view.
