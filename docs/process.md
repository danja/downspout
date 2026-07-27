# Downspout development process

## Overview

Downspout is developed through close collaboration between human maintainers,
testers, and AI coding assistants. AI assistance is used extensively: agents
inspect the repository, translate requirements and field feedback into proposed
changes, implement code and documentation, run automated checks, and keep build,
release, screenshot, and catalog metadata synchronized.

The AI is an engineering tool, not the final authority. Maintainers set product
direction, approve material scope changes, judge musical and user-interface
quality, validate plugins in real hosts, control credentials, review changes,
and decide what is committed or published. Test output and host observations are
evidence; an assistant saying that something works is not.

## Repository instructions and reusable skills

Every task starts by reading [AGENTS.md](../AGENTS.md). It defines the repository
rules, architecture, documentation expectations, and completion checklist. Its
important constraints include:

- never expose API keys or other credentials;
- do not perform Git operations without explicit user approval;
- keep portable DSP, MIDI, state, and transport logic separate from DPF glue;
- prefer plugin-local changes and obtain approval before changing shared code,
  framework dependencies, or root build/release infrastructure;
- preserve source-plugin behavior before redesigning a port;
- configure a fresh CMake build directory completely before starting a build;
- keep tests deterministic and documentation current.

Agents also use task-specific instructions under [`.codex/skills/`](../.codex/skills/).
The current repository skills cover:

- adding a plugin end to end;
- extending an existing plugin;
- selecting and running build/test verification;
- maintaining release packaging, screenshots, and the Pages catalog.

Skills encode repeatable checklists and known repository hazards. They reduce
the chance that an apparently local change misses a parameter bridge, state
field, test target, release bundle, screenshot, or catalog entry. Reusable
project-facing skills also live under [`skills/`](../skills/); these snapshots
belong in the repository even if a developer has a personal installed copy.

Instructions are applied in layers: the maintainer's request defines the goal,
`AGENTS.md` defines repository policy, and the relevant skill supplies the
workflow. If requirements remain materially ambiguous after inspecting the
code and docs, the agent asks rather than making a consequential product choice.

## Development loop

### 1. Capture the requirement

Work begins with a concrete request, bug report, design note, or external test
report. Feedback should be preserved close to the affected work, with the
source acknowledged when appropriate. The assistant separates observations
from proposed actions before editing code.

The proposal should identify:

- user-visible behavior to change;
- portable-core behavior versus wrapper/UI work;
- compatibility implications for parameters and saved state;
- tests that can reproduce the issue or protect the intended behavior;
- documentation, screenshots, packaging, or catalog entries affected.

### 2. Inspect before editing

The agent reads the plugin README and local design/porting notes, then traces the
relevant implementation from portable types and engine code through state,
parameters, the DPF wrapper, UI, and tests. Existing reference plugins are used
as patterns where helpful.

This stage also establishes scope. A plugin-local fix stays inside
`plugins/<plugin>/` whenever possible. Changes to `third_party/`, `src/common/`,
`include/downspout/`, shared scripts, or root build/release glue require explicit
approval because they can affect every plugin.

### 3. Implement from the core outward

Downspout follows one architectural sequence:

1. implement deterministic behavior in portable C++;
2. clamp and sanitize parameter/state inputs;
3. define or extend stable text serialization when state changes;
4. expose the behavior through thin DPF parameter, transport, audio, MIDI, and
   state adapters;
5. update the NanoVG UI without duplicating processor logic;
6. add or update deterministic core tests;
7. update plugin-local documentation.

DPF remains the shell rather than the architecture. The intended dependency
direction is:

```text
DAW host -> DPF wrapper and UI -> portable Downspout core
```

For ports, source LV2 parameter IDs, ranges, defaults, transport behavior, and
state assumptions remain traceable in the plugin documentation. Original
plugins follow the same layering even when there is no LV2 behavior to preserve.

### 4. Verify proportionally

Verification starts with the narrowest meaningful target and broadens with the
risk of the change. A typical plugin check is:

```bash
cmake --build build --target downspout_<plugin>_core_tests
./build/plugins/<plugin>/downspout_<plugin>_core_tests
cmake --build build --target <plugin>-vst3
ctest --test-dir build -R downspout_<plugin>_core_tests --output-on-failure
```

If `build/CMakeCache.txt` does not exist, configure first and wait for it to
finish before building:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
```

Tests focus on portable behavior and include edge cases appropriate to the
plugin: malformed state, range clamping, silence, bounded audio, rapid parameter
changes, MIDI ordering, stopped transport, first-play blocks, loops, rewinds,
tempo or meter changes, and deterministic regeneration.

Wrapper and UI changes must also compile the real plugin bundle. DAW-facing
problems may additionally be checked with Steinberg's VST3 validator and compared
with a known working Downspout bundle so framework-wide limitations are not
misreported as plugin-local success or failure.

### Windows cross-build and Wine validation

A Windows VST3 can be cross-built locally on Linux with MinGW. Configure the
build completely before starting it:

```bash
cmake -S . -B build/windows-x86_64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DDOWNSPOUT_BUILD_SIDECAR=OFF \
  -DDOWNSPOUT_BUILD_AI_COORDINATOR=OFF
cmake --build build/windows-x86_64 --target <plugin>-vst3 --parallel
```

Sidecar is excluded because it has Linux-only runtime dependencies. Cross-built
core tests are also disabled in this package-oriented build; portable behavior
should already have passed the native Linux tests.

For Windows-specific VST3 conformance checks, cross-build Steinberg's
`validator.exe` from the VST3 SDK, run its self-test under Wine, and then pass
it the cross-built bundle:

```bash
WINEDEBUG=-all WINEPREFIX=/tmp/downspout-wine-vst3 \
  wine64 /path/to/validator.exe -selftest

WINEDEBUG=-all WINEPREFIX=/tmp/downspout-wine-vst3 \
  wine64 /path/to/validator.exe \
  build/windows-x86_64/bin/<plugin>.vst3 \
  > /tmp/<plugin>-windows-validator.log 2>&1
```

Some distributions provide `wine` instead of `wine64`. Treat Wine validation
as failed if the process exits nonzero or the log reports failed tests, even
when earlier checks succeeded. Record the final `Result:` line and relevant bus
metadata; validator output for MIDI plugins can be very large.

This check exercises the Windows bundle and binary through Wine, but it does
not prove editor behavior, host-specific routing, scanning, or loading in a
native Windows DAW. A real Windows host test remains required for issues such
as the Ableton compatibility report. CI and release Windows packages use the
DPF `win64` action path; Wine validation is currently an additional local
diagnostic rather than a release-workflow gate. See
[cross-platform.md](cross-platform.md) for the current platform status.

Automated tests do not replace listening and host validation. Effects and
instruments are tested in real sessions for routing, automation, saved-state
recall, transport behavior, bypass, controller interaction, visual usability,
and audible artifacts. External tester feedback is fed back into the same loop.

## Documentation and visual review

Behavioral or workflow changes update the plugin README and local docs in the
same change. Material project-direction changes also update
[plan.md](plan.md). Architecture, installation, and release documents are kept
aligned with the actual code and automation rather than future intent.

Public plugin copy lives under [`docs/pages/_products/`](pages/_products/).
GitHub Pages screenshots are captured from the real DPF standalone UI—not from
mockups—using:

```bash
scripts/capture-plugin-screenshots.sh <plugin>
```

The generated image under `docs/pages/assets/plugins/` is visually inspected for
clipping, overlap, stale controls, misleading status displays, and readability.
UI issues revealed by the screenshot are fixed and the asset is regenerated.
See [screenshots.md](screenshots.md) and [pages/README.md](pages/README.md).

The Pages workflow in [`.github/workflows/pages.yml`](../.github/workflows/pages.yml)
builds and deploys the catalog from `docs/pages/`.

## Build, install, and packaging

[`install.sh`](../install.sh) is the normal local build/install entry point. It
configures the project, builds enabled plugins, runs CTest by default, and
installs VST3 bundles into the configured plugin directory. A DAW rescan may be
required after installation because the script does not modify host caches.

Release packaging is performed by
[`scripts/package-release.sh`](../scripts/package-release.sh). It runs a release
configure/build/test/install/package cycle and produces a versioned zip plus a
SHA-256 checksum. Plugin CMake install declarations, `install.sh`, packaging
expectations, CI, release workflow assets, documentation, and public catalog
entries must remain synchronized.

Continuous integration is defined in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml).
It builds, tests, installs, packages, and stores artifacts. Linux is the required
package build; macOS and Windows probes expose cross-platform failures before
those artifacts are treated as proven.

## Publishing

Publishing is a deliberate maintainer action, not an automatic consequence of
an AI completing code changes. The normal release path is:

1. review the implementation, documentation, generated assets, and test results;
2. validate affected plugins in suitable DAWs and operating systems;
3. commit and push the reviewed changes;
4. create and push a new `v*` version tag;
5. monitor the GitHub Actions release workflow;
6. verify the published archives, checksums, and installation behavior.

The release workflow in
[`.github/workflows/release.yml`](../.github/workflows/release.yml) publishes
platform packages to GitHub Releases. Tags are not reused after a failed
release; the problem is fixed and a new version is issued. Full details are in
[release.md](release.md).

## Definition of done

A plugin change is complete when all applicable items below are true:

- the requirement and any assumptions are documented;
- domain behavior remains in the portable core and DPF glue remains thin;
- parameters, defaults, ranges, state, wrapper bridges, and UI agree;
- deterministic regression tests cover the changed behavior;
- the relevant core tests and plugin-format targets pass;
- significant host-facing behavior has been checked in a host or clearly marked
  as awaiting host validation;
- plugin README/design notes describe the current behavior;
- public Pages copy and the real-UI screenshot are refreshed after visible UI
  changes;
- install, package, workflow, and bundle lists agree when release contents
  change;
- known framework or platform limitations are recorded honestly;
- a maintainer has reviewed the result and controls the Git and publication
  steps.

This process deliberately combines fast AI-assisted iteration with portable
architecture, executable evidence, real-host feedback, and human release
judgment.
