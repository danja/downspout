# Tops

Hi-hat and cymbal generator: ten instruments (closed, open and pedal hi-hat, crash, ride,
splash, china, cowbell, tambourine, triangle), six engines (metallic, noise, FM, physical,
wavetable, ring mod) and five presets (rock, jazz, electronic, metal, latin).

- Bundle: `tops.vst3` (instrument, 0 in / 2 out, MIDI in).
- MIDI: velocity sets level. **Note Map** `General MIDI` (default): notes 42 closed, 44 pedal, 46 open,
  49 crash, 51 ride, 52 china, 54 tambourine, 55 splash, 56 cowbell, 81 triangle (also 53/59 ride,
  57 crash); other notes are ignored. `Selected`: any note plays the instrument chosen by the
  Instrument parameter. A closed or pedal hat chokes a sounding open hat.
- State: ordinary parameters only; the host saves them.
- Choosing a Preset in the panel sets the related sound controls once; they remain ordinary parameters.
- Tests: `downspout_tops_core_tests`. Parameter and MIDI mapping: `docs/design.md`.
