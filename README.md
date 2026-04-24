# downspout

`downspout` is a from-scratch repository for native plugin ports of selected [flues](https://github.com/danja/flues) LV2 projects, with an initial focus on VST3-capable builds via DPF ([DISTRHO Plugin Framework](https://distrho.github.io/DPF/)).

## Install From Releases

On a typical Linux system, download the latest
`downspout-<version>-linux-x86_64-vst3.zip` from GitHub Releases, unpack it, and
copy the `.vst3` bundles into `~/.vst3`:

```bash
mkdir -p ~/.vst3
cp -r bassgen.vst3 p_mix.vst3 drumgen.vst3 cadence.vst3 ~/.vst3/
```

Then restart your DAW or trigger a plugin rescan.

The immediate targets are:

- `bassgen`: a transport-aware MIDI generator
- `p-mix`: a transport-aware probabilistic audio effect
- `drumgen`: a transport-aware MIDI drum generator
- `cadence`: a transport-aware MIDI harmonizer and comping generator

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
- a first DPF-backed `p-mix` VST3 wrapper target with UI;
- a portable `drumgen` core library with a host-neutral MIDI engine, serialization helpers, tests, and a first DPF-backed VST3 wrapper target with UI;
- a portable `cadence` core library with tests and a first DPF-backed `cadence.vst3` wrapper target with UI;
- an `install.sh` entrypoint for local VST3 installs.

`bassgen`, `p_mix`, `drumgen`, and `cadence` can now be built and installed as `.vst3` bundles.

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
- `drumgen.vst3`
- `cadence.vst3`

## Releases

Release packaging is handled by `scripts/package-release.sh` and automated on
GitHub Actions for tags matching `v*`. The initial public artifact is a Linux
x86_64 VST3 zip containing all current bundles.

See [docs/release.md](docs/release.md) for the release artifact shape and
workflow details.

## Drumgen

`drumgen` now builds as `drumgen.vst3` on top of the portable core and engine.
It now has a first custom DPF UI with dedicated `New`, `Mutate`, and `Fill`
buttons. The per-plugin plan lives under `plugins/drumgen/docs/`.

## Cadence

`cadence` now builds as `cadence.vst3` on top of a portable harmony, comping,
variation, and transport core. It has a first custom DPF UI and is ready for
host-side validation in a DAW.

## Next steps

1. Finish host-side validation of `bassgen.vst3` in Reaper and fix any remaining wrapper/UI issues.
2. Continue validating the new stereo `p-mix.vst3` wrapper and its manual mute toggle in Reaper.
3. Tighten any remaining host-specific `p-mix` UI or interaction issues beyond the first layout polish pass.
4. Validate `cadence.vst3` in Reaper, especially learning, restart/rewind behavior, and state restore.
5. Validate the GitHub Actions release workflow on the first public tag that includes all current plugins.
6. Verify `install.sh` and local docs against a clean `Release` build path for all current plugins.
7. Validate `drumgen.vst3` in Reaper, especially the new action-button UI, transport sync, and state restore.
