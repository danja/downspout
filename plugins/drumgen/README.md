# drumgen port

This directory holds the `downspout` port of `~/github/flues/lv2/drumgen`.

Current focus:

- keep the portable core aligned with the existing MIDI-generator split;
- preserve pattern persistence and loop-boundary variation behavior;
- validate the thin DPF/VST3 wrapper against real hosts;
- keep genre and style additions deterministic and state-compatible.

Current reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`

Implementation status:

- plugin scaffold now exists in `downspout`;
- CMake wiring now exists behind `DOWNSPOUT_BUILD_DRUMGEN` and is now enabled by default;
- per-plugin audit and extraction docs now exist;
- portable core type definitions now exist;
- portable pattern, transport, variation, and state-sanitization code now exists;
- a host-neutral MIDI scheduling engine now exists;
- text serialization for controls, pattern state, and variation state now exists;
- deterministic core and engine tests now exist and pass;
- a DPF-backed `drumgen.vst3` wrapper now builds with a custom control UI;
- the core now follows shared meter input beyond bar length alone: compound and
  triple meters get dedicated pulse-aware anchor and fill behavior;
- the wrapper now exposes explicit `Auto`, `Straight`, `Reel`, `Waltz`, `Jig`,
  `Slip Jig`, and `Diddley` style modes so users can force rhythmic vocabulary
  instead of relying only on meter-derived auto behavior. `Diddley` pins a
  two-bar "shave and a haircut, two bits" 3-2 accent shape with clave,
  kick/snare reinforcement, and light hats;
- genres now include Breakbeat, Amen, Jungle, Hip Hop, Jazz, and Fugue
  alongside the earlier Rock, Disco, Shuffle, Electro, Dub, Motorik, Bossa, and
  Afro options. Fugue intentionally produces a sparse pulse rather than a full
  drum style. Rock now pins a harder kick/backbeat/hat signature in Auto style
  so it lands as a direct rock groove before variation and fills are added.

Recommended next steps:

1. validate `drumgen.vst3` in a host, especially action buttons, genre/style controls, and saved-state restore;
2. broaden tests around earlier-state-format handling where it still applies to the current wrapper-facing state mapping;
3. decide whether a preview grid is worth adding after host validation.

Current wrapper behavior note:

- the manual `Fill` action now targets the current bar when there is still room
  to hear the fill, otherwise it targets the next bar, and it forces a stronger
  fill amount than the passive fill slider alone.
