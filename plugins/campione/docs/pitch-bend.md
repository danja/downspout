# Pitch bend

Campione follows MIDI pitch bend **per channel**, so it can be driven by a
per-channel retuner such as Retune (one note per channel, each with its own bend).

- Each voice follows the bend of the channel its note arrived on. Two notes held on
  different channels bend independently, and a bend on one channel never moves a
  voice on another.
- The range is the **Bend Range** control (0-24 semitones, default 2), the same for
  every channel. Retune's own *Bend range* must be set to the same value. Campione
  does not read RPN 0.
- Bend is applied to the playback rate of the ringing voice, so it takes effect
  immediately and also bends a release tail. It is reset by `activate()`.
- The global *MIDI channel* filter applies to bend as it does to notes: with a single
  channel selected, bends on other channels are ignored.

Zones can be pinned to one MIDI channel (`SampleZone::midiChannel`). A zone pinned to
channel 1 will not sound for a note Retune sends on channel 5, so leave zones on
*all channels* when using a per-channel retuner.

Tested by `testPitchBendIsPerChannel` in `tests/campione_core_tests.cpp`.
