# drumgen port

This directory will hold the `downspout` port of `~/github/flues/lv2/drumgen`.

Current focus:

- keep the portable core aligned with the existing `bassgen` MIDI-generator split;
- preserve exact pattern persistence and loop-boundary variation behavior;
- validate the thin DPF/VST3 wrapper against real hosts;
- keep UI scope behind correct transport, MIDI, and state behavior.

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
- a first DPF-backed `drumgen.vst3` wrapper now builds with a custom control UI.

Recommended next steps:

1. validate `drumgen.vst3` in a host, especially the action buttons and saved-state restore path;
2. broaden tests to cover legacy state upgrade behavior and wrapper-facing state mapping;
3. decide whether a preview grid is worth adding after host validation;
4. keep release packaging aligned with the now-UI-backed wrapper target.
