# bassgen porting notes

Source plugin: `~/github/flues/lv2/bassgen`

Notable source concerns to preserve:

- MIDI note generation
- host transport sync
- rewind-aware playback reset
- persisted pattern state
- variation behavior across loop boundaries

Likely reusable source modules:

- pattern generation
- variation logic
- transport interpretation
- state serialization
