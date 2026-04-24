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
