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
  derives pulse accents, pickups, and longer landmark note lengths from it, so
  `bassgen` no longer treats every bar like a flat quarter-beat grid even
  though its genre vocabulary is still broad rather than folk-specific;
- the wrapper now exposes explicit `Style` modes for `Auto`, `Straight`,
  `Reel`, `Waltz`, `Jig`, and `Slip Jig`, and those modes are implemented in
  the portable pattern generator rather than only as UI labels

Likely reusable source modules:

- pattern generation
- variation logic
- transport interpretation
- state serialization
