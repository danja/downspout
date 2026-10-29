# Clatter

General MIDI percussion generator: the sounds that Thumper (kick), Snatch (snare) and Tops (hi-hats and
cymbals) do not cover. 31 sounds in five synthesis families, each on its General MIDI note.

| Family | Sounds (GM note) |
|---|---|
| Membrane | toms 41 43 45 47 48 50; bongos 60 61; congas 62 (mute) 63 (open) 64; timbales 65 66; surdos 86 (mute) 87 (open) |
| Metal | agogos 67 68; mute triangle 80; jingle bell 83; bell tree 84 |
| Wood | claves 75; wood blocks 76 77; castanets 85 |
| Shaker | cabasa 69; maracas 70; shaker 82; vibraslap 58 |
| Clap + guiro | hand clap 39; guiros 73 (short) 74 (long) |

Not covered anywhere yet: whistles (71, 72) and cuicas (78, 79).

- Bundle: `clatter.vst3` (instrument, 0 in / 2 out, MIDI in).
- MIDI: velocity sets level, note-off is ignored. **Note Map** `General MIDI` (default): only the
  notes above play. `Selected`: any note plays the sound chosen with the Sound parameter or the panel.
- Per-family **Tune** (-12 to +12 st), **Decay** (x0.25 to x4) and **Tone** (0 to 1, neutral at 0.5), plus
  **Level** and **Width** (stereo spread of each sound's fixed pan; 0.6 is the designed spread). Each family also has a **Pan** offset (-1 to +1, 0 = designed placement).
- Choke groups: open and mute conga, open and mute surdo, short and long guiro cut each other.
- 16 voices; the oldest is stolen. No plugin state: every setting is an ordinary parameter.
- Tests: `downspout_clatter_core_tests`. Design notes: `docs/design.md`.
