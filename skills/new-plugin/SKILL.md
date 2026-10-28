---
name: new-plugin
description: Add a new Downspout plugin end to end (portable core, serialization, DPF wrapper, Magneto UI, tests, build/install/release lists, docs, screenshot). Use when asked to create or port a plugin, or to check a plugin is wired into everything.
---

# Add a Downspout plugin

`CLAUDE.md` is authoritative (working rules, "New plugin completion checklist"). This skill is the order of work and the verification commands. Never run `git` without approval, and do not edit shared code (`third_party/`, `src/common/`, `include/downspout/`, shared scripts, root build glue) without approval. Adding the plugin's own lines to the root CMake/install/release lists is the expected exception; ask once up front.

Reference shapes: `plugins/clatter` (smallest complete recent plugin), `plugins/magneto` (look and feel), `p-mix`/`e-mix`/`rift` (audio effects).

## 1. Layout

```
plugins/<name>/
  CMakeLists.txt        core STATIC lib + dpf_add_plugin + install(DIRECTORY ...vst3) + downspout_add_core_test
  include/<name>_params.hpp  <name>_core_types.hpp  <name>_serialization.hpp  [<name>_rng.hpp]
  src/<name>_engine.cpp  src/<name>_serialization.cpp
  src/dpf/DistrhoPluginInfo.h  <Name>Plugin.cpp  <Name>UI.cpp
  tests/<name>_core_tests.cpp
  docs/design.md  README.md  profile.ttl
```
Copy `plugins/clatter/CMakeLists.txt` and rename. Core knows nothing about DPF.

## 2. Build in this order (build after each step)

1. Params struct + defaults + ranges (core). Defaults here must equal the DPF wrapper defaults and UI defaults; a mismatch showed up as "Layer Mix 50 %" on a fresh instance.
2. Engine (`render`, `trigger`/`process`), tests as you go (see `plugin-core-tests`).
3. Text serialization with optional fields defaulting for old states, clamping, and rejection of garbage.
4. DPF wrapper: parameters map 1:1 to Params. Use `WANT_STATE` only for non-parameter data, and then with `WANT_FULL_STATE`. `activate()` / `sampleRateChanged()` clear runtime state only.
5. UI (see `magneto-ui`).
6. Docs and lists (section 3).

## 3. Everything that must mention the plugin

- root `CMakeLists.txt`: `option(DOWNSPOUT_BUILD_<NAME> ...)` + `add_subdirectory(plugins/<name>)`
- `install.sh` (`-DDOWNSPOUT_BUILD_<NAME>=ON`, required bundles), `scripts/package-release.sh` required_bundles, `scripts/package-built-bundles.sh`, `.github/workflows/release.yml`
- `scripts/capture-plugin-screenshots.sh` (`"<name>:<name>"`), then `docs/pages/assets/plugins/<name>.png`
- `README.md` table, `docs/install.md`, `docs/release.md`, `docs/architecture.md`, `docs/plan.md`, `docs/screenshots.md`, `docs/summary.md`
- `docs/pages/_products/<name>.md` (front matter) and `docs/pages/plugins-summary.md` (paragraph + table row)
- `plugins/<name>/profile.ttl` (update on every structural change afterwards)
- `MISTAKES.md` if anything went wrong

## 4. Verify (all of it, report failures verbatim)

```
cmake --build build --target <name>-vst3 -j8 2>&1 | grep -E "error:|Error [0-9]"   # also check exit status
ctest --test-dir build -R <name> --output-on-failure
scripts/check-bundle-lists.sh
scripts/check-plugin-state.sh
./install.sh  (or the plugin's install path), then scripts/inspect-plugin.sh <name>
scripts/capture-plugin-screenshots.sh --output-dir <scratchpad> <name>
```
- `cmake --build --target <name>` alone does not compile the UI; build `<name>-vst3`.
- Confirm installed `.so` files are not zero bytes.
- Open the screenshot at full resolution and review it as a first-time user (CLAUDE.md "UI screenshot review"). Only then copy it into `docs/pages/assets/plugins/`.
- `inspect-plugin.sh` reports "NUM_INPUTS=0 but IS_SYNTH not set" for MIDI generators (drumgen, melgen, ground, bassgen); that is a known generic warning, not a regression.
- Say plainly what was not verified (not tried in Reaper, not heard).
