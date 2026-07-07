# Plugin Extension Patterns

## Reference shapes

- Audio effects: `p-mix`, `e-mix`, and `rift` are the best current reference shapes.
- Original transport-aware buffer/DSP effect: `rift`.
- Probabilistic switching and output status parameters: `p-mix`, `e-mix`.
- MIDI/generator transport logic: `bassgen`, `drumgen`, `ground`, `counterpointer`.
- Sample-backed audio path: `rift`.

## Layer boundaries

Portable core files normally live in `plugins/<plugin>/include/` and `plugins/<plugin>/src/` outside `src/dpf/`. Core code should not include DPF headers. DPF wrappers convert host concepts into portable structs and call the core.

Keep these concepts portable:

- parameter structs and defaults;
- state serialization and migration defaults;
- DSP/MIDI/event scheduling;
- transport snapshots and block-boundary logic;
- deterministic random decisions;
- output status values consumed by the UI.

Keep these concepts in DPF glue:

- DPF parameter metadata;
- DPF `TimePosition` conversion;
- VST3/plugin metadata;
- UI widgets, NanoVG drawing, file browser hooks;
- host state key declarations and forwarding.

## Parameter checklist

When adding a host-visible parameter:

1. Add the core field with default.
2. Clamp it in core sanitization.
3. Serialize it with a new text-state version if state text is versioned.
4. Deserialize old states by leaving the field at its default when missing.
5. Append the parameter ID unless there is a strong reason to reorder.
6. Add DPF parameter metadata with min/max/default/hints.
7. Add get/set cases and trigger edge logic if relevant.
8. Add UI storage initialization, current parameter conversion, control drawing, and input handling.
9. Add tests for clamp/default/serialization plus at least one behavior test.

## State checklist

State text should be stable and tolerant of older versions. Unknown keys may intentionally fail if the local parser already does that, but missing new keys should keep the default unless changing behavior is explicit.

For stateful UI lanes or patterns, test round trip and malformed/out-of-range cell handling.

## DSP behavior checklist

- Avoid heap allocation inside hot audio loops unless the existing plugin already does it and the allocation is bounded/outside the callback path.
- Keep random behavior deterministic from engine state.
- Crossfade discontinuous read-head or state changes when the existing plugin has transition smoothing.
- For wet/dry behavior, document whether the dry signal is live input, generated sample source, or processed source.
