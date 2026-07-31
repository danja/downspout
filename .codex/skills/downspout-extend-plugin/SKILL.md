---
name: downspout-extend-plugin
description: Extend or modify behavior in an existing downspout plugin. Use when Codex is asked to add controls, DSP or MIDI behavior, transport/state handling, serialization, UI elements, or tests inside a plugin directory while preserving the portable-core plus thin-DPF-wrapper architecture.
---

# Downspout Extend Plugin

Use this skill for changes inside an existing `plugins/<plugin>/` directory.

## Workflow

1. Read `AGENTS.md`, the plugin `README.md`, and any plugin `docs/` notes before editing.
2. Identify the host-agnostic behavior first. Keep DSP, MIDI, scheduling, state normalization, and serialization in the portable core.
3. Keep DPF code as glue: parameter declarations, host transport conversion, state forwarding, audio/MIDI buffer adaptation, and UI.
4. Prefer plugin-local edits. Do not modify `third_party/`, `src/common/`, `include/downspout/`, root CMake/install/release scripts, or shared workflows unless the user explicitly approves.
5. Update tests before or with behavior changes. Prefer deterministic core tests over manual DAW verification.
6. Update plugin-local docs when behavior, state, parameters, transport assumptions, or UI workflow changes.

## Common edit paths

For parameters and controls, update in this order:

- `plugins/<plugin>/include/*_core_types.hpp` or equivalent parameter struct and defaults.
- Clamp/sanitize logic in the core.
- Text serialization/deserialization and old-state fallback defaults.
- `plugins/<plugin>/include/*_params.hpp` parameter IDs, appending new IDs when compatibility matters.
- `src/dpf/*Plugin.cpp` `initParameter`, `getParameterValue`, `setParameterValue`, and any output-status mapping.
- `src/dpf/*UI.cpp` local UI state, control definitions, current-parameter conversion, interaction handling, and layout.
- `tests/*_core_tests.cpp` coverage for defaults, clamp, serialization, and behavior.

For transport-aware behavior, explicitly test stopped transport, first play block, loop/rewind boundaries, tempo/grid changes, and hold/recover/scatter-style triggers when present.

For UI changes, remove or move text before shrinking functional controls. Keep labels readable at the fixed plugin window size and make controls reflect processor output status where possible.

After a material UI change, capture the actual rendered plugin screenshot and
inspect it at full resolution as a first-time user. Check purpose and signal
flow, primary workflow, grouping, labels, values, units, modes, routing, live
status, useful defaults, clipping, crowding, and legibility. Iterate and
recapture until the interface is understandable without reading source code.
If capture or inspection cannot run locally, report visual acceptance as
explicitly pending.

Read [references/plugin-patterns.md](references/plugin-patterns.md) when you need concrete file patterns, reference plugins, or parameter/state checklists.
