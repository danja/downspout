# cadence port

This directory holds the `downspout` port of `~/github/flues/lv2/cadence`.

Current focus:

- preserve the learned-progression and transport-synced harmony behavior from the
  LV2 source;
- keep the harmony and variation logic portable and testable outside DPF;
- keep the DPF wrapper thin around transport, MIDI, state, and UI plumbing;
- validate the first VST3 wrapper in a real DAW before widening scope.

Current reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`
- `docs/porting.md`

Implementation status:

- plugin scaffold now exists in `downspout`;
- portable core types, harmony generation, comping, transport, variation, and
  serialization code now exist;
- a host-neutral cadence engine now exists;
- deterministic cadence core tests now exist and pass;
- a first DPF-backed `cadence.vst3` wrapper now builds with a custom control UI.
- scale choices now include Lydian, Melodic Minor, Whole Tone, Altered,
  diminished, and bebop colors, appended after the original scale IDs so
  saved-state scale values remain stable;
- the `Color` control adds harmonic tension, biasing Jazz scales toward ii-V-I
  motion, stronger V-I resolution, sevenths, dominant color, and
  diminished/tritone-sub options. On major/minor-style scales, high `Color`
  also biases ambiguous learned material toward circle-of-fifths support and
  suspended dominant preparation;
- the `Extended` chord-size mode expands stored chord slots to six notes and
  can generate 9th, 11th, and 13th voicings while Triads and Sevenths remain
  available for simpler output;
- `Spread` is now a continuous voicing-width control, and `Arpeggio` thins
  playback from solid chords toward rotating single-note chord tones.

Recommended next steps:

1. validate `cadence.vst3` in Reaper, especially the learn cycle, restart/rewind
   handling, and saved-state restore path;
2. validate the continuous `Spread` and `Arpeggio` controls against dense
   Extended voicings and higher `Comp` settings;
3. broaden tests around earlier-state-format handling and wrapper-facing state mapping;
4. keep release packaging aligned with the now-public wrapper target.

## Host compatibility

The DPF/VST3 wrapper exposes a silent stereo output bus for compatibility with
hosts that reject event-only plugins with no audio outputs. The portable core
remains MIDI-only, and the wrapper clears both output channels every block.
