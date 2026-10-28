# Thumper

Kick drum generator: ten synthesis engines (virtual analog, modal, subtractive, FM, phase
distortion, wavetable, 808, 909, physical, noise burst), seven style presets (techno,
gabber, rock, latin, house, trap, acoustic) plus Custom, independent pitch, filter and amp
envelopes, an optional second engine layer and output distortion.

- Bundle: `thumper.vst3` (instrument, 0 in / 2 out, MIDI in).
- MIDI: velocity sets level, note-off is ignored. **Note Map** `General MIDI` (default): only
  notes 35 and 36 play, always at the Pitch parameter. `Selected`: any note-on triggers a kick; on
  channel 10 it plays at the Pitch parameter, elsewhere the note sets the pitch of that hit only (the
  Pitch parameter is not changed), so the kick can be played as a bass line.
- State: ordinary parameters only; the host saves them.
- Choosing a Preset in the panel sets the related sound controls once; they remain ordinary parameters.
- Tests: `downspout_thumper_core_tests`. Parameter and MIDI mapping: `docs/design.md`.
