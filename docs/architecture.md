# downspout architecture

## Overview

`downspout` is organized around one repeated pattern:

1. extract or write a host-agnostic C++ core;
2. keep that core testable without DPF or a DAW;
3. wrap it in a thin DPF shell for VST3;
4. keep plugin-specific docs next to the plugin.

That is the main architectural decision in the repository. DPF is the shell,
not the architecture.

## Top-level build graph

The root [CMakeLists.txt](/home/danny/github/downspout/CMakeLists.txt) does four things:

- defines the project and common compiler settings;
- creates `downspout-project-options`, an interface target for shared include
  paths and C++ feature requirements;
- discovers DPF from either `DPF_ROOT` or `third_party/DPF`;
- conditionally adds each plugin subdirectory with `DOWNSPOUT_BUILD_*` options.

This means incomplete or optional plugins can be turned off without breaking the
rest of the tree.

## Dependencies

Build-time dependencies:

- CMake 3.20+
- C/C++ toolchain with C++20 support
- DPF for wrapper/UI/plugin-format targets
- Linux UI/OpenGL/X11 development packages when building DPF UIs on Linux

Repository dependencies:

- `third_party/DPF/` is the main vendored framework dependency
- no plugin core is supposed to depend on DPF headers directly

Runtime dependency direction is intentionally one-way:

`DAW host -> DPF wrapper/UI -> downspout core`

The portable cores do not know about VST3, DPF widgets, or host objects.

## Per-plugin structure

Each plugin directory generally follows this shape:

- `include/`
  Plugin-local public headers for the portable core
- `src/`
  Core implementation
- `src/dpf/`
  DPF wrapper, metadata, and UI
- `tests/`
  Deterministic core tests
- `docs/`
  Audit notes, extraction plan, porting notes
- `README.md`
  Human-facing summary of behavior and current status

The common build pattern, visible for example in
[plugins/rift/CMakeLists.txt](/home/danny/github/downspout/plugins/rift/CMakeLists.txt), is:

- a static core library such as `downspout_rift_core`
- sometimes an interface port target such as `downspout_rift_port`
- an optional DPF plugin target such as `rift-vst3`
- an optional core test executable added under `BUILD_TESTING`

## Code layers

### 1. Portable core

This is where the actual behavior belongs.

Typical contents:

- core types and parameter structs
- transport mapping types
- scheduling / DSP / MIDI generation logic
- state sanitization
- text serialization

Examples:

- [plugins/bassgen/src/bassgen_engine.cpp](/home/danny/github/downspout/plugins/bassgen/src/bassgen_engine.cpp)
- [plugins/drumgen/src/drumgen_engine.cpp](/home/danny/github/downspout/plugins/drumgen/src/drumgen_engine.cpp)
- [plugins/rift/src/rift_engine.cpp](/home/danny/github/downspout/plugins/rift/src/rift_engine.cpp)

### 2. Wrapper layer

The DPF plugin class translates host concepts into core concepts:

- DPF parameters -> core parameter structs
- DPF `TimePosition` -> portable transport snapshots
- audio/MIDI buffers -> core block views
- trigger parameters -> serial counters or edge-trigger actions
- core output status -> UI-visible output parameters

Examples:

- [plugins/p-mix/src/dpf/PMixPlugin.cpp](/home/danny/github/downspout/plugins/p-mix/src/dpf/PMixPlugin.cpp)
- [plugins/cadence/src/dpf/CadencePlugin.cpp](/home/danny/github/downspout/plugins/cadence/src/dpf/CadencePlugin.cpp)

### 3. UI layer

The custom UIs are NanoVG-based DPF panels. They are supposed to make the
plugin legible, not reimplement the engine.

Patterns used repeatedly:

- keep UI state driven by parameters and output-status parameters
- use momentary trigger buttons for one-shot actions
- expose live processor status back to the UI through output parameters when
  host automation values alone are not enough

Examples:

- [plugins/drumgen/src/dpf/DrumgenUI.cpp](/home/danny/github/downspout/plugins/drumgen/src/dpf/DrumgenUI.cpp)
- [plugins/rift/src/dpf/RiftUI.cpp](/home/danny/github/downspout/plugins/rift/src/dpf/RiftUI.cpp)

## State and serialization pattern

For stateful plugins, `downspout` prefers explicit text serialization instead of
burying state inside wrapper-specific storage logic.

Typical split:

- `*_state.*` for validation, normalization, and upgrade/sanitization logic
- `*_serialization.*` for stable textual save/load representation

This keeps state format concerns separate from DSP and separate from DPF.

## Transport pattern

Transport-aware plugins use small portable snapshot structs rather than
directly reading host APIs in core code. The wrapper converts host timing into a
core snapshot once per block, and the engine handles:

- stopped transport
- play/stop transitions
- loop boundaries
- rewind / restart behavior
- tempo and bar-grid calculations

That pattern is especially visible in `bassgen`, `drumgen`, `cadence`, and
`rift`.

## Testing pattern

Tests target the portable core, not the DPF wrapper.

That keeps them:

- deterministic
- fast
- runnable in CI without a DAW

Current test coverage is mostly behavior-level unit tests under each plugin’s
`tests/` directory. Wrapper behavior is validated primarily through local DAW
testing and release/install smoke runs.

## Install and release path

Local install flow:

- [install.sh](/home/danny/github/downspout/install.sh) configures, builds,
  optionally runs `ctest`, and installs `.vst3` bundles into `~/.vst3` by
  default

Release flow:

- [scripts/package-release.sh](/home/danny/github/downspout/scripts/package-release.sh)
  performs a release configure/build/test/install/package cycle
- [.github/workflows/release.yml](/home/danny/github/downspout/.github/workflows/release.yml)
  runs that script on `v*` tags and publishes the zip to GitHub Releases

The release script currently expects these bundles:

- `bassgen.vst3`
- `p_mix.vst3`
- `e_mix.vst3`
- `rift.vst3`
- `drumgen.vst3`
- `cadence.vst3`
- `gremlin.vst3`
- `gremlin_driver.vst3`

So yes: current `rift` changes are part of the release packaging path as soon
as you build and tag a release from updated code.

## Conventions followed in this repo

- prefer portable C++ cores over framework-coupled logic
- keep DPF wrappers thin
- keep docs close to the plugin they describe
- keep tests focused on deterministic core behavior
- keep plugin options independently switchable in CMake
- avoid broad shared abstractions until at least two plugins clearly need them
