# drumgen port

This directory will hold the `downspout` port of `~/github/flues/lv2/drumgen`.

Current focus:

- keep the portable core aligned with the existing `bassgen` MIDI-generator split;
- preserve exact pattern persistence and loop-boundary variation behavior;
- map the finished engine onto a thin DPF/VST3 wrapper next;
- keep UI scope behind correct transport, MIDI, and state behavior.

Current reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`

Implementation status:

- plugin scaffold now exists in `downspout`;
- guarded CMake wiring now exists behind `DOWNSPOUT_BUILD_DRUMGEN`;
- per-plugin audit and extraction docs now exist;
- portable core type definitions now exist;
- portable pattern, transport, variation, and state-sanitization code now exists;
- a host-neutral MIDI scheduling engine now exists;
- text serialization for controls, pattern state, and variation state now exists;
- deterministic core and engine tests now exist and pass;
- no DPF wrapper code exists yet.

Recommended next steps:

1. add the DPF/VST3 wrapper on top of the new engine and serialization layer;
2. broaden tests to cover legacy state upgrade behavior and wrapper-facing state mapping;
3. validate the wrapper in a host before deciding on UI expansion;
4. decide whether to port a preview grid immediately or defer it until the wrapper is host-stable.
