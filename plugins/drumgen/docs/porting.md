# drumgen porting notes

Source plugin: `~/github/flues/lv2/drumgen`

Notable source concerns to preserve:

- transport-synced polyphonic MIDI drum generation;
- deterministic genre-biased lane pattern generation from controls plus seed;
- explicit fill overlay behavior on the last bar;
- loop-aware `Vary` behavior that mutates only at loop boundaries;
- persisted exact pattern and variation state, not just control values.

Likely reusable source modules:

- pattern generation and cleanup
- variation logic
- transport interpretation
- state sanitization

Current wrapper choice:

- the first DPF/VST3 wrapper now has a thin custom control UI with explicit
  `New`, `Mutate`, and `Fill` buttons;
- the wrapper now also exposes explicit `Style` modes for `Auto`, `Straight`,
  `Reel`, `Waltz`, `Jig`, and `Slip Jig`, and those modes drive core timing
  behavior rather than only post-generation labels;
- the manual `Fill` trigger is intentionally more immediate than the original
  stored-last-bar refresh: it now targets the current or next bar and boosts the
  fill amount so the result is audible in host use;
- the `Resolution` control includes `1/4`, `1/8`, `1/16`, and `1/16T`; `1/4`
  is appended to the saved-state enum so existing resolution values keep their
  previous meaning;
- the lowest `Density` setting now thins nonessential anchors and Euclidean
  pulses more aggressively, especially hats and auxiliary percussion, while
  leaving core downbeat/backbeat skeleton behavior intact;
- the portable pattern core now uses shared meter data for more than bar size:
  `6/8`, `9/8`, `12/8`, and `3/4` get pulse-aware anchor and fill landmarks
  instead of only scaled `4/4` quarter-slot logic;
- the genre set now includes `Breakbeat`, `Amen`, `Jungle`, `Hip Hop`,
  `Jazz`, and `Fugue`. The breakbeat-family genres use core-side signature overlays for
  syncopated kicks, backbeat snares, ghost snares, and denser hat/open-hat
  figures; Jazz uses a core-side swing ride shape with feathered quarter-note
  kick and light snare comping while leaving explicit non-auto style modes in
  control; Rock pins a harder kick/backbeat/hat signature in Auto style; Fugue
  strips the generated kit back to a sparse metrical pulse so it can sit under
  contrapuntal material without fills or crash-heavy behavior;
- the port preserves exact control/state behavior first and still leaves any
  preview-grid UI as follow-up work after host validation.
