# bassgen porting notes

Source plugin: `~/github/flues/lv2/bassgen`

Notable source concerns to preserve:

- MIDI note generation
- host transport sync
- rewind-aware playback reset
- persisted pattern state
- variation behavior across loop boundaries

Current `downspout`-specific port note:

- the portable core now persists a normalized `Meter` in pattern state and
  derives pulse accents from it, so `bassgen` no longer treats every bar like a
  flat quarter-beat grid even though its genre vocabulary is still broad rather
  than folk-specific

Likely reusable source modules:

- pattern generation
- variation logic
- transport interpretation
- state serialization
