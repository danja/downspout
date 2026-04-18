# downspout

`downspout` is a from-scratch repository for native plugin ports of selected [flues](https://github.com/danja/flues) LV2 projects, with an initial focus on VST3-capable builds via DPF ([DISTRHO Plugin Framework](https://distrho.github.io/DPF/)).

The immediate targets are:

- `bassgen`: a transport-aware MIDI generator
- `p-mix`: a transport-aware probabilistic audio effect

The repository is intentionally scaffolded around a shared-core pattern:

- `plugins/<plugin>/`
  Plugin-specific DSP wrapper, UI wrapper, metadata, and docs.
- `src/common/`
  Shared utilities that are safe to reuse across plugins.
- `include/downspout/`
  Shared public headers for reusable code.
- `tests/`
  Unit and host-facing regression tests.
- `third_party/`
  Vendored external dependencies once pinned and approved.

## Status

This repository now contains:

- project planning and scaffolding;
- a portable `bassgen` core library with tests;
- a first DPF-backed `bassgen` VST3 wrapper target with UI;
- a portable `p-mix` core library with tests;
- a first DPF-backed `p-mix` VST3 wrapper target;
- an `install.sh` entrypoint for future VST3 installs.

`bassgen` and `p-mix` can now be built and installed as `.vst3` bundles.

## Build & Install

To build and install the current VST3 output:

```bash
./install.sh
```

This will:

- configure the project in `./build`
- build the project
- run `ctest`
- install VST3 bundles into `~/.vst3` by default

Useful overrides:

```bash
DOWNSPOUT_VST3_DIR=/some/other/path ./install.sh
DOWNSPOUT_RUN_TESTS=0 ./install.sh
```

Current installable plugin:

- `bassgen.vst3`
- `p_mix.vst3`

## Next steps

1. Finish host-side validation of `bassgen.vst3` in Reaper and fix any remaining wrapper/UI issues.
2. Validate `p-mix.vst3` in Reaper and confirm the 8-channel wrapper behaves correctly on multichannel tracks.
3. Decide whether `p-mix` should remain wrapper-first or gain a minimal UI in the next pass.
4. Add an explicit release-build workflow and document the expected release artifacts.
5. Verify `install.sh` and local docs against a clean `Release` build path for both plugins.
6. Use the `bassgen` and `p-mix` wrapper patterns as the baseline for the next plugin port.
