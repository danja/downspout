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

- the first DPF/VST3 wrapper is intentionally thin and uses the host's generic
  parameter UI;
- the port preserves exact control/state behavior first and leaves any custom
  preview-grid UI as follow-up work after host validation.
