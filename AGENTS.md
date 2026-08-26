# downspout agent notes


## Working rules
- API keys are sacred. They must not be shared.
- Do not run any `git` operations unless the user explicitly approves them.
- Keep DSP/domain logic separate from framework glue.
- Prefer portable C++ for shared logic; isolate DPF-, OS-, and format-specific code behind thin wrappers.
- Preserve the behavior of the source LV2 plugins before attempting UI or feature redesign.
- Build the repository so that incomplete plugins can coexist without breaking unrelated work.
- Do not vendor new third-party code unless the user asked for it or the task clearly requires it.
- Do not modify shared/framework code that existing plugins rely on without explicit user approval first. This includes `third_party/`, `src/common/`, `include/downspout/`, shared scripts, and root build/install/release glue.
- Document assumptions when mapping LV2 concepts to DPF/VST3, especially transport, state, and UI behavior.
- On a fresh CMake build directory, do not launch the build command until configure has finished. In this repo, starting `cmake --build` too early regularly races cache generation and fails with `Error: could not load cache`.
- Log mistakes in MISTAKES.md (what happened, root cause, prevention).
- Periodically review docs/todo.md and revise as necessary.

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
- `scripts/`
  Reusable repository automation, including parameterized REAPER composition
  and MIDI-generation helpers. Keep reusable scripts here rather than in
  temporary directories.
- `skills/`
  Repository-visible snapshots of reusable agent skills. Personal installed
  copies may live outside the repository and are not a substitute for the
  version kept here.
- `third_party/`
  Approved vendored dependencies such as DPF.

## REAPER composition guidance

- Read `docs/summary.md` before selecting Downspout generators, instruments,
  effects, or their ordering in a REAPER project.
- Preserve the current REAPER session before creating a new project, batch MIDI
  note creation where possible, and save the editable `.RPP` before rendering.
- Treat MCP tool response fields as authoritative. Do not claim that an effect,
  render, or analysis succeeded when the response reports an error or timeout.
- Save reusable composition logic under `scripts/` and repository skills under
  `skills/`; do not use `/tmp` for durable artifacts.

## Porting guidance

- Start by identifying host-agnostic behavior in the `flues` source plugin.
- Move deterministic logic into small testable classes before wiring DPF.
- Treat DPF as the shell, not the architecture.
- Current working pattern in this repo is: portable core, text state
  serialization, thin DPF wrapper, custom NanoVG UI, and deterministic core
  tests. `p-mix`, `e-mix`, and `rift` are the reference shapes for audio
  effects.
- Keep parameter IDs, ranges, defaults, and semantic behavior traceable back to the original plugin.
- For transport-aware plugins, write explicit tests for stopped transport, loop boundaries, rewind, and tempo/bar changes.
- For stateful plugins, define a stable serialization contract before UI work.
- When an LV2 plugin has separate note/control MIDI ports that do not map cleanly to VST3, prefer one primary MIDI input in the DPF wrapper and keep role-detection inside the portable processor.
- For controller-heavy ports, expose output status parameters for the UI so the panel can reflect the processor’s effective live state rather than only the last host automation value.

## Documentation expectations

- Keep docs concise, technical, and current.
- Update `docs/plan.md` when project direction changes materially.
- Record per-plugin migration decisions in that plugin’s `docs/` directory rather than bloating the root plan.
- On any structural change to a plugin (new or removed parameter, changed range or default, changed MIDI/audio routing, added CC mapping): update that plugin’s `profile.ttl` to reflect the change in the same commit.
- On any UI change to a plugin: ensure that plugin appears in `scripts/capture-plugin-screenshots.sh` and note that a fresh screenshot is needed.

## UI screenshot review

- Treat a rendered screenshot as a product review artifact, not merely catalog
  output. After adding a plugin or materially changing its UI, capture the real
  standalone/plugin interface and inspect the image at full resolution.
- Review it as a first-time user: the plugin's purpose and signal flow should be
  apparent; primary controls should have clear names, values, units, grouping,
  and hierarchy; modes, routing contracts, and live status should be visible;
  defaults should communicate useful behavior; and text or controls must not be
  clipped, crowded, ambiguous, or too small.
- Iterate on the UI and recapture the screenshot until the main workflow is
  understandable without consulting source code. Do not call a new or changed
  UI ready solely because it compiles or renders.
- If screenshot capture or image inspection is unavailable, record that exact
  limitation and leave visual acceptance explicitly pending.

## New plugin completion checklist

When adding a new plugin, keep the local build, release artifact, and GitHub
Pages catalog in sync in the same change:

- add the plugin-local `CMakeLists.txt`, root `CMakeLists.txt` build option,
  and `install(DIRECTORY ...vst3)` rule;
- update `install.sh`, `scripts/package-release.sh`, and
  `.github/workflows/release.yml` so local installs, CI packaging, and tagged
  releases include the bundle;
- update `README.md`, `docs/install.md`, `docs/release.md`,
  `docs/architecture.md`, and `docs/plan.md` where the bundle list or project
  status changes;
- add `docs/pages/_products/<plugin>.md` with front matter, add the plugin to
  `scripts/capture-plugin-screenshots.sh`, and generate
  `docs/pages/assets/plugins/<plugin>.png`;
- inspect the generated screenshot from a first-time user's perspective using
  the UI screenshot review criteria above, then revise and recapture as needed;
- update `docs/screenshots.md` and `docs/pages/README.md` when screenshot or
  Pages processes change;
- run at least the plugin core tests, the plugin VST3 target build, shell
  syntax checks for install/release scripts, and a static check that
  `scripts/package-release.sh` expected bundles match plugin CMake install
  declarations.
