# downspout

`downspout` is a from-scratch repository for native plugin ports of selected [flues](https://github.com/danja/flues) LV2 projects, with an initial focus on VST3-capable builds via DPF ([DISTRHO Plugin Framework](https://distrho.github.io/DPF/)).

## Note on authorship

Most of the plugin ideas in this repository came from Danny and the earlier
`flues` work. `rift` is the exception: it is the one original plugin concept
proposed and built in `downspout` by the coding agent rather than ported from
an existing Danny plugin.

## Install From Releases

On a typical Linux system, download the latest
`downspout-<version>-linux-x86_64-vst3.zip` from GitHub Releases, unpack it, and
copy the `.vst3` bundles into `~/.vst3`:

```bash
mkdir -p ~/.vst3
cp -r bassgen.vst3 p_mix.vst3 e_mix.vst3 rift.vst3 drumgen.vst3 cadence.vst3 gremlin.vst3 gremlin_driver.vst3 ground.vst3 ~/.vst3/
```

Then restart your DAW or trigger a plugin rescan.

The immediate targets are:

- `bassgen`: a transport-aware MIDI generator
- `p-mix`: a transport-aware probabilistic audio effect
- `e-mix`: a transport-aware Euclidean audio gate
- `rift`: an original transport-aware buffer damage effect
- `drumgen`: a transport-aware MIDI drum generator
- `cadence`: a transport-aware MIDI harmonizer and comping generator
- `gremlin`: a chaotic glitch instrument with live performance gestures
- `gremlin-driver`: a MIDI modulation and action sequencer for `gremlin`
- `ground`: an original long-form MIDI bass form generator

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
- a portable `e-mix` core library with tests;
- a first DPF-backed `e_mix.vst3` wrapper target with a redesigned UI;
- an original `rift` core library with tests and a first DPF-backed `rift.vst3` wrapper target with UI;
- a portable `drumgen` core library with a host-neutral MIDI engine, serialization helpers, tests, and a first DPF-backed VST3 wrapper target with UI;
- a portable `cadence` core library with tests and a first DPF-backed `cadence.vst3` wrapper target with UI;
- a portable `gremlin` core library with tests and a first DPF-backed `gremlin.vst3` wrapper target with UI;
- a portable `gremlin-driver` MIDI control core with tests and a first DPF-backed `gremlin_driver.vst3` wrapper target with UI;
- an original `ground` core library with tests and a first DPF-backed `ground.vst3` wrapper target with UI;
- an `install.sh` entrypoint for local VST3 installs.

`bassgen`, `p_mix`, `e_mix`, `rift`, `drumgen`, `cadence`, `gremlin`, `gremlin_driver`, and `ground` can now be built and installed as `.vst3` bundles.

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
- `e_mix.vst3`
- `rift.vst3`
- `drumgen.vst3`
- `cadence.vst3`
- `gremlin.vst3`
- `gremlin_driver.vst3`
- `ground.vst3`

## Releases

Release packaging is handled by `scripts/package-release.sh` and automated on
GitHub Actions for tags matching `v*`. The initial public artifact is a Linux
x86_64 VST3 zip containing all current bundles.

See [docs/release.md](docs/release.md) for the release artifact shape and
workflow details.

## Reference Docs

- [docs/architecture.md](docs/architecture.md)
  How the repository is wired together, including build graph, dependency
  direction, core/wrapper/UI split, and install/release flow
- [docs/install.md](docs/install.md)
  Local build and installation notes
- [docs/release.md](docs/release.md)
  Release packaging and GitHub Release workflow
- [docs/requirements.md](docs/requirements.md)
  Build and dependency expectations
- [docs/plan.md](docs/plan.md)
  Current project direction and remaining work

Each plugin also has its own `README.md` and `docs/` directory under
`plugins/<plugin>/` for port-specific notes.

## EMix

`e_mix` now builds as `e_mix.vst3` on top of a portable stereo-first transport
gate core. Its custom UI is a deliberate redesign of the old LV2 panel: the
cycle pattern, block length, density, and fade envelope are all visible
directly.

## Rift

`rift` now builds as `rift.vst3`. It is an original `downspout` effect rather
than a `flues` port, and it is the one plugin idea in this repository that did
not originate with Danny: a transport-locked stereo buffer disruptor with
repeat, reverse, skip, smear, and pitch-slip actions plus `Hold`, `Scatter`,
and `Recover` performance controls.

## Drumgen

`drumgen` now builds as `drumgen.vst3` on top of the portable core and engine.
It now has a first custom DPF UI with dedicated `New`, `Mutate`, and `Fill`
buttons. The per-plugin plan lives under `plugins/drumgen/docs/`.

## Cadence

`cadence` now builds as `cadence.vst3` on top of a portable harmony, comping,
variation, and transport core. It has a first custom DPF UI and is ready for
host-side validation in a DAW.

## Gremlin

`gremlin` now builds as `gremlin.vst3` on top of a portable chaotic synth core
and a DPF wrapper/UI that collapses the LV2 note and controller inputs into one
VST3 MIDI input path. The custom UI exposes scenes, actions, macros, live
damage controls, and momentary hold pads.

## GremlinDriver

`gremlin_driver` now builds as `gremlin_driver.vst3` on top of a portable MIDI
modulation engine and a DPF wrapper/UI. It is intended to sit in front of
`gremlin.vst3` in a MIDI chain and emit macro CCs, action notes, and patch
randomisation bursts while passing note input through.

## Ground

`ground` now builds as `ground.vst3`. It is an original `downspout` MIDI
generator that works above the riff level: it plans bass movement across
phrases and sections, then emits monophonic lines that aim toward climbs,
pedals, cadences, and releases rather than only mutating a short loop.

## Next steps

1. Finish host-side validation of `bassgen.vst3` in Reaper and fix any remaining wrapper/UI issues.
2. Continue validating the new stereo `p-mix.vst3` wrapper and its manual mute toggle in Reaper.
3. Validate `e_mix.vst3` in Reaper, especially stopped transport behavior, restart handling, and UI readability.
4. Validate `rift.vst3` in Reaper, especially whether the macro panel and block preview make the effect learnable in practice.
5. Tighten any remaining host-specific `p-mix` UI or interaction issues beyond the first layout polish pass.
6. Validate `cadence.vst3` in Reaper, especially learning, restart/rewind behavior, and state restore.
7. Validate `gremlin.vst3` in Reaper, especially controller-style MIDI gestures, scene changes, and output level.
8. Validate `gremlin_driver.vst3` in Reaper, especially MIDI pass-through, transport sync, and chaining into `gremlin.vst3`.
9. Validate `drumgen.vst3` in Reaper, especially the new action-button UI, transport sync, and state restore.
10. Validate `ground.vst3` in Reaper, especially long-form phrase behavior, action triggers, and state restore.
11. Publish the first public `v*` tag and verify the GitHub Actions release assets on GitHub.
