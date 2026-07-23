# Xoxolo implementation notes

Xoxolo combines a manual MIDI pattern editor with an explicit, user-triggered
pattern generator.

Implementation decisions:

- the grid is stored in `PatternState` and serialized as text under the
  `pattern` state key;
- cells are not exposed as automatable parameters;
- UI edits call DPF `setState(...)`, while Steps, Resolution, Channel, Note
  Preset, Clear, and Preview remain small wrapper parameters;
- `xoxolo_engine` owns transport-to-step mapping, note scheduling, preview
  triggering, and pending note-offs;
- MIDI input is not passed through in the first pass;
- patterns use an explicit `8..32` step count, so odd lengths are supported and
  the full pattern remains visible.

Generation decisions:

- `xoxolo_generator` is portable and has no DPF/UI dependencies;
- six fixed 16-slot style templates separately describe anchors, syncopated
  tension targets, and optional fills;
- Density controls anchor retention and optional activity, while Tension
  increases syncopations and late-phrase fills;
- generation accepts an explicit seed, making core behavior deterministic in
  tests while successive UI clicks produce new variations;
- generated hits replace the visible grid and are persisted through the
  existing `pattern` state contract;
- Style, Density, and Tension are drafting controls local to the UI. They do
  not add plugin parameters or alter the stable serialized pattern format;
- voice-to-lane mapping is isolated in the generator so both Downspout and
  AVL-Drumkits note presets retain their existing MIDI-note assignments.

The default `Downspout` lane map mirrors `drumkit.vst3` so Xoxolo can
immediately drive the Downspout drum synth without setup. The `AVL-Drumkits`
preset follows the note names from `docs/avl-drum-map.md` and exposes all 29
kit entries from notes 36 through 64.
