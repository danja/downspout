# Thumper design notes

Portable core (`thumper_engine`, `thumper_serialization`) behind a thin DPF wrapper.

## Parameters (DPF order)
Engine 0-9, Preset 0-7, Pitch 30-120 Hz (def 55), Pitch Drop 0-24 st (12), Decay 0.05-2 s
(0.5), Click, Body, Drive, Tone, Volume (0.8), FM Ratio 0.5-8, FM Index 0-10, Modal Decay,
Sub Mix, Distortion, Pitch Env / Filter Env / Amp Env decay 0.001-2 s, Engine 2 0-9, Layer
Mix 0-1 (def 0, i.e. no audible layer), Note Map 0-1 (Selected / General MIDI; default General MIDI).

## Assumptions
- MIDI: note-on triggers a kick; velocity (0-1) scales level. Note Map General MIDI (default): only notes 35 and 36
  play, always at the Pitch parameter. Selected: channel 10 plays at the Pitch parameter; other channels play at
  the note frequency for that hit only (the Pitch parameter is unchanged).
  Note-off is ignored; one voice per layer, so a new hit retriggers.
- The core's "no layer" (`engine2 = -1`) cannot be set from the host (Engine 2 minimum is
  0); the layer is silent only while Layer Mix is 0.
- No transport use. No plugin state: every setting is an ordinary parameter, which the host saves. The text `serialize`/`deserialize` in the core is used by tests and tools; `deserialize` rejects out-of-range selectors and clamps values.
- Preset is applied by the UI, not the DSP: selecting one sends engine, pitch, drop, decay, click, body and drive as ordinary parameters (Custom sends nothing). Modal Decay sets the decay of the Modal engine's upper partials relative to the amp envelope (neutral at 0.4).
- Wavetable is a saw that morphs to a sine over the hit (it was previously identical to Subtractive).
