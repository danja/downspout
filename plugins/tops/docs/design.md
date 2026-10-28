# Tops design notes

Portable core (`tops_engine`, `tops_serialization`) behind a thin DPF wrapper.

## Parameters (DPF order)
Instrument 0-9 (closed, open, pedal hat, crash, ride, splash, china, cowbell, tambourine,
triangle), Engine 0-5 (metallic, noise, FM, physical, wavetable, ring mod), Preset 0-4
(rock, jazz, electronic, metal, latin), Pitch 1000-12000 Hz (8000), Decay 0.01-2 s (0.08),
Tone, Noise, Brightness, Volume, Distortion, Note Map 0-1 (Selected / General MIDI; default General MIDI).

## Assumptions
- MIDI: velocity (0-1) scales level. Note Map Selected plays the Instrument parameter for any
  note; General MIDI maps notes 42/44/46/49/51/52/53/54/55/56/57/59/81 to instruments and ignores
  the rest. Eight-voice pool; closed and pedal hats choke open hats.
- Instruments share an engine but are voiced differently: decay scale (crash x4, ride x2.5, open x2,
  china x1.5, cowbell x3, triangle x6, tambourine x0.6; closed/pedal fixed 0.05/0.07 s), pitch scale
  (china x0.75, tambourine x1.3, cowbell x0.08) and extra noise (china +0.2, tambourine +0.3).
- Tone is a high-frequency tilt around 3 kHz, neutral at the 0.6 default. The envelope falls about
  70 dB over the voice's life (the previous version sped up as it decayed).
- Preset is applied by the UI, not the DSP: selecting one sends decay, brightness and noise as ordinary parameters. 
