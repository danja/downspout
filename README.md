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
- a first DPF-backed `bassgen` VST3 wrapper target;
- a portable `p-mix` core library with tests;
- an `install.sh` entrypoint for future VST3 installs.

`bassgen` can now be built and installed as a `.vst3` bundle. `p-mix` is not wrapped yet.

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

## Next steps

1. Vendor or pin DPF in `third_party/DPF`.
2. Finish extracting host-agnostic logic from the `flues` LV2 targets into reusable core modules.
3. Validate `bassgen` in a real VST3 host.
4. Add the first DPF VST3 build for `p-mix`.
5. Extend `install.sh` as more wrappers become installable.
