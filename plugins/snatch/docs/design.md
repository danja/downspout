# Snatch design notes

Portable core (`snatch_engine`, `snatch_serialization`) behind a thin DPF wrapper.

## Parameters (DPF order)
Engine 0-5 (tone+noise, metal, brush, electronic, acoustic, side stick), Preset 0-4 (rock, jazz,
electronic, latin, brushed), Pitch 100-400 Hz (200), Tone, Noise (0.7), Snap (0.6), Body
(0.6), Decay 0.05-1 s (0.25), Volume (0.8), Distortion, Note Map 0-1 (Selected / General MIDI; default General MIDI).

## Assumptions
- MIDI: note-on with velocity > 0 triggers one monophonic voice; velocity (0-1) scales level.
  Note Map Selected plays the Engine parameter for any note; General MIDI maps 37 to Side Stick,
  38 to the selected engine, 40 to Electronic and ignores the rest. Channel and note-off are ignored.
- Side Stick is a short wooden click (a high partial plus a little noise) whose decay is capped at 0.05 s.
- No transport use. No plugin state: every setting is an ordinary parameter, which the host saves. The text `serialize`/`deserialize` in the core is used by tests and tools; `deserialize` rejects out-of-range selectors and clamps values.
- Preset is applied by the UI, not the DSP: selecting one sends engine, pitch, noise, snap and decay as ordinary parameters.
