# downspout agent notes


## Working rules
- API keys are sacred. They must not be shared.
- Do not run any `git` operations unless the user explicitly approves them.
- Use the Read tool to read files, not `sed`/`cat`/`head`/`tail` via Bash. Bash tool calls require per-call user approval; Read does not.
- Keep DSP/domain logic separate from framework glue.
- Prefer portable C++ for shared logic; isolate DPF-, OS-, and format-specific code behind thin wrappers.
- Preserve the behavior of the source LV2 plugins before attempting UI or feature redesign.
- Build the repository so that incomplete plugins can coexist without breaking unrelated work.
- Do not vendor new third-party code unless the user asked for it or the task clearly requires it.
- Do not modify shared/framework code that existing plugins rely on without explicit user approval first. This includes `third_party/`, `src/common/`, `include/downspout/`, shared scripts, and root build/install/release glue.
- Document assumptions when mapping LV2 concepts to DPF/VST3, especially transport, state, and UI behavior.
- On a fresh CMake build directory, do not launch the build command until configure has finished. In this repo, starting `cmake --build` too early regularly races cache generation and fails with `Error: could not load cache`.
- Register a plugin's core tests with `downspout_add_core_test(<target> SOURCES ... LIBS ...)` (`cmake/DownspoutTests.cmake`): it applies `-UNDEBUG`, the shared include path and `add_test`. Do not copy the long form.
- Test suites that use `assert()` must set `-UNDEBUG` on their target and include `downspout/test_assert.h`. The Release build adds `-DNDEBUG`, which compiles asserts out so a suite passes while testing nothing; the header turns that into a compile error.
- Log mistakes in MISTAKES.md (what happened, root cause, prevention).
- Periodically review TODO.md and revise as necessary.
- **`docs/scales.md` is the primary reference for all scale-related work** — canonical ordering, per-plugin coverage, naming conventions, and the append-only rule. Consult it before adding, renaming, or reordering scales in any plugin.
- periodically read INBOX.md and insert tasks as appropriate in TODO.md then clear out INBOX.md
- periodically check the `dim` MCP tools (`dim_endpoints`, `dim_search`, `dim_get`) for any Farelo tasks relevant to Downspout, and fold them into TODO.md as appropriate. DIM writes are real, so treat it as read-only for this check unless the user asks otherwise
- All new plugins will have the basic look & feel of plugins/magneto with the choice of switches, dropdown select lists, rotary knobs, sliders, x-y controls or novel editable components decided on a case-by-case basis

## Lessons from past mistakes

Distilled from the retired MISTAKES.md; keep adding new recurring patterns to MISTAKES.md first and promote them here.

**Tests**
- Assert the observable effect, not an intermediate (e.g. the emitted note, not `noteForCell`). To make a parameter inert, assert its default is inert rather than zeroing it. Test that every control has an audible/visible effect.
- Re-run a plugin's tests after changing any parameter default.
- Table-driven data (scales, ladders): hold an independent expected set in the test, diff against existing tables elsewhere in the repo first, and generate docs from the source enum, not from the reference it was derived from.
- Transport-quantised emitters need a schedule queue in absolute quarters; events outside the current block must not be dropped.
- Probes must advance the input pointer per block, and be sanity-checked against the raw input.
- A doc that quotes a "worst case over a range" needs a test sweeping the whole range, asserting the documented bound.
- Measure before building: write the accuracy probe (pitch ratio, filter response across the whole parameter space) before wiring DSP. Use a Goertzel magnitude, not zero-crossings, on spliced signals. Read the reference function (e.g. `plinky.c` `stride()`) before describing it.
- Measuring a tone's size: sum the Hann main-lobe energy (`A = sqrt(32/3 x sum|X|^2) / N`) and use the same window length as the render. A single peak bin loses up to 15 % to scalloping and silently skews ratios. Treat DC as harmonic 0 and exclude +-16 bins around each harmonic when measuring aliasing.
- Before concluding the DSP is wrong, derive the reference from the formula for the stated order and check one point by hand (a 4-pole is -40 log10(1 + m^2), not half that). When a metric mixes two effects (resonant peak and passband loss), neutralise one before measuring the other.
- A bound on output level is the wrong stability test for cascaded boosts (they legitimately exceed 100 dB): check that the impulse response decays.
- Build output: filter with `grep -E "error:|Error [0-9]"` and check the exit status. After adding a test, confirm it actually ran (count/runtime changed) before saying it passes.

**Host lifecycle and state**
- `WANT_STATE` without `WANT_FULL_STATE` makes the host save defaults from `initState()` into every project. `Plugin`'s third constructor argument is *stateCount*. Run `scripts/check-plugin-state.sh`. DPF already saves ordinary parameters, so state is only for non-parameter data.
- `activate()` and `sampleRateChanged()` must clear runtime state only, never call `init()` or reset the patch. Cover with a `testHostActivationKeepsThePatch`-style test.
- Core tests never touch the DPF wrapper, so a green build proves nothing about state or lifecycle: change a setting, save, reopen in a host.
- Recursive filters on idle voices decay into denormals (4x CPU): skip idle voices, zero their state, flush tiny values per block, and time new recursive DSP over a long decay.

**Screenshots and UI**
- Run `scripts/capture-plugin-screenshots.sh <plugin>`; it always captures on a private Xvfb (it used to photograph the user's real desktop when `DISPLAY` was set, twice), so no `env -u DISPLAY` is needed. Capture into the scratchpad with `--output-dir`, open the image at full resolution, and only then copy it into the repo.
- Capture by PID or window id, never `xdotool search --name` (substring match). `identify` the PNG against `DISTRHO_UI_DEFAULT_WIDTH/HEIGHT`. A blank grey capture means raise `DOWNSPOUT_SCREENSHOT_SETTLE_SECONDS`.
- Generate stepper label tables from the value and clamp the index; give an empty state no mark. Derive plot shapes numerically and compare with the image.

**Process**
- Never `pkill -f` / `pgrep -f` with a pattern that appears in your own command line; use `ps` or the bracket trick (`foo[.]bar`).
- Rerun a timing-based test ("renders faster than real time") alone before treating it as a regression: it failed under a load average of 37 and passed alone. A CPU figure taken on a busy machine is an upper bound; say so.
- `cmake --build --target <plugin>` does not compile the UI (it builds a stub library). Build `<plugin>-vst3` and check the exit status; after changing a shared header such as `MagnetoKit.hpp`, build `-vst3` for every plugin that includes it (`grep -rl MagnetoKit.hpp plugins`). Include what a UI file uses (`<string>`): the screenshot (jack) build can hide a missing include.
- Scripted batch edits: `continue` in every branch, diff the result. For non-unique lines anchor on context, not first match.
- Port a DSP core a function at a time and build after each; do not draft the whole thing in one pass.
- Cross-repo scripts: read the dataclass before constructing it positionally, and distrust a large mismatch on only some cases.

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
- When documenting MCP server tools, take argument names, types, and defaults from the tool’s `inputSchema` in the server source code, not from existing prose. Copy-pasteable examples must be verified against the schema.

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
- add the plugin to `docs/pages/plugins-summary.md` (a paragraph plus a row in the
  quick-reference table: ports, MIDI roles, CC map, key parameters). Update it too when
  an existing plugin's ports, MIDI roles, CC map or key parameters change;
- run at least the plugin core tests, the plugin VST3 target build, shell
  syntax checks for install/release scripts, and a static check that
  `scripts/package-release.sh` expected bundles match plugin CMake install
  declarations.
