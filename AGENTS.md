# downspout agent notes

## Purpose

`downspout` exists to port selected `flues` LV2 plugins into more broadly usable native plugin formats, with DPF as the current framework candidate and VST3 as the primary initial target.

Initial plugin targets:

- `bassgen`: MIDI generator with transport sync and persistent pattern state
- `p-mix`: audio effect with transport-aware probabilistic switching

## Working rules

- Do not run any `git` operations unless the user explicitly approves them.
- Keep DSP/domain logic separate from framework glue.
- Prefer portable C++ for shared logic; isolate DPF-, OS-, and format-specific code behind thin wrappers.
- Preserve the behavior of the source LV2 plugins before attempting UI or feature redesign.
- Build the repository so that incomplete plugins can coexist without breaking unrelated work.
- Do not vendor new third-party code unless the user asked for it or the task clearly requires it.
- Document assumptions when mapping LV2 concepts to DPF/VST3, especially transport, state, and UI behavior.
- On a fresh CMake build directory, do not launch the build command until configure has finished. In this repo, starting `cmake --build` too early regularly races cache generation and fails with `Error: could not load cache`.

## Repository shape

- `plugins/<plugin>/src/`
  Plugin-specific implementation and framework entry points.
- `plugins/<plugin>/include/`
  Plugin-local headers.
- `plugins/<plugin>/docs/`
  Porting notes, parameter mapping, and test notes.
- `src/common/`
  Shared reusable code extracted from multiple plugins.
- `include/downspout/`
  Shared public headers.
- `tests/`
  Automated tests. Prefer deterministic tests over manual-only verification.
- `third_party/`
  Approved vendored dependencies such as DPF.

## Porting guidance

- Start by identifying host-agnostic behavior in the `flues` source plugin.
- Move deterministic logic into small testable classes before wiring DPF.
- Treat DPF as the shell, not the architecture.
- Current working pattern in this repo is: portable core, text state
  serialization, thin DPF wrapper, custom NanoVG UI, and deterministic core
  tests. `p-mix` and `e-mix` are the reference shapes for audio effects.
- Keep parameter IDs, ranges, defaults, and semantic behavior traceable back to the original plugin.
- For transport-aware plugins, write explicit tests for stopped transport, loop boundaries, rewind, and tempo/bar changes.
- For stateful plugins, define a stable serialization contract before UI work.
- When an LV2 plugin has separate note/control MIDI ports that do not map cleanly to VST3, prefer one primary MIDI input in the DPF wrapper and keep role-detection inside the portable processor.
- For controller-heavy ports, expose output status parameters for the UI so the panel can reflect the processor’s effective live state rather than only the last host automation value.

## Documentation expectations

- Keep docs concise, technical, and current.
- Update `docs/plan.md` when project direction changes materially.
- Record per-plugin migration decisions in that plugin’s `docs/` directory rather than bloating the root plan.
