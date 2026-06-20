# Xoxolo implementation notes

The first Xoxolo slice is intentionally small and direct. It is a manual MIDI
pattern editor, not a probabilistic drum generator.

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

The default `Downspout` lane map mirrors `drumkit.vst3` so Xoxolo can
immediately drive the Downspout drum synth without setup. The `AVL-Drumkits`
preset follows the note names from `docs/avl-drum-map.md` and exposes all 29
kit entries from notes 36 through 64.
