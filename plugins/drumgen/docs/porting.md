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
- the manual `Fill` trigger is intentionally more immediate than the original
  stored-last-bar refresh: it now targets the current or next bar and boosts the
  fill amount so the result is audible in host use;
- the portable pattern core now uses shared meter data for more than bar size:
  `6/8`, `9/8`, `12/8`, and `3/4` get pulse-aware anchor and fill landmarks
  instead of only scaled `4/4` quarter-slot logic;
- the port preserves exact control/state behavior first and still leaves any
  preview-grid UI as follow-up work after host validation.
