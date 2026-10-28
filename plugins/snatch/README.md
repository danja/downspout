# Snatch

Snare drum generator: six engines (tone+noise, metal, brush, electronic, acoustic, side stick) and five
presets (rock, jazz, electronic, latin, brushed), with Pitch, Tone, Noise, Snap, Body, Decay
and output distortion.

- Bundle: `snatch.vst3` (instrument, 0 in / 2 out, MIDI in).
- MIDI: velocity sets level. **Note Map** `General MIDI` (default): 37 side stick, 38 acoustic snare
  (the selected engine), 40 electric snare (Electronic engine); other notes are ignored. `Selected`:
  any note plays the chosen engine.
- State: ordinary parameters only; the host saves them.
- Choosing a Preset in the panel sets the related sound controls once; they remain ordinary parameters.
- Tests: `downspout_snatch_core_tests`. Parameter and MIDI mapping: `docs/design.md`.
