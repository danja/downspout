# install

`install.sh` is the intended user-facing build/install entrypoint for local VST deployment.

## Default behavior

- configures the project in `./build`
- builds the configured targets
- installs to `~/.vst3` by default
- runs `ctest` before install by default

## Environment overrides

- `DOWNSPOUT_BUILD_DIR`
  Override the build directory.
- `DOWNSPOUT_VST3_DIR`
  Override the VST3 install directory.
- `CMAKE_BUILD_TYPE`
  Override the CMake build type. Default is `Release`.
- `DPF_ROOT`
  Override the DPF location if you want to use an external checkout.
- `DOWNSPOUT_RUN_TESTS`
  Set to `0` to skip `ctest` during install.

## Release builds

Release builds are now an explicit project requirement.

Current default behavior already configures with `CMAKE_BUILD_TYPE=Release`.
Public release packaging is handled separately by
`scripts/package-release.sh`; see `docs/release.md`.

## Current outputs

The repository currently installs eighteen real wrapper targets:

- `bassgen.vst3` with UI
- `p_mix.vst3` with UI
- `e_mix.vst3` with UI
- `m_mix.vst3` with UI
- `melgen.vst3` with UI
- `rift.vst3` with UI
- `drumgen.vst3` with UI
- `drumkit.vst3` with UI
- `cadence.vst3` with UI
- `counterpointer.vst3` with UI
- `gremlin.vst3` with UI
- `gremlin_driver.vst3` with UI
- `ground.vst3` with UI
- `floozy.vst3` with UI
- `basilico.vst3` with UI
- `canticle.vst3` with UI
- `luma.vst3` with UI
- `paunchlad.vst3` with UI
- `lifeform.vst3` with UI

The next install-related validation is host-side confirmation that all eighteen bundles behave correctly in `Release` builds.

`install.sh` updates the VST3 bundles, but it does not modify DAW plugin
caches. If a DAW still shows stale names, makers, categories, or homepage
metadata after install, force that DAW to rescan VST3 plugins or clear its
plugin cache. Known examples include REAPER's `reaper-vstplugins64.ini` and
Ardour's `~/.cache/ardour*/vst` cache files.

## Verified behavior

The script has been smoke-tested with a temporary install root under `/tmp`:

- configure
- build
- test
- install
- confirmed `bassgen.vst3` bundle output and install
- confirmed `p_mix.vst3` bundle output and install
- confirmed `e_mix.vst3` bundle output and install
- confirmed `m_mix.vst3` bundle output and install
- confirmed `melgen.vst3` bundle output and install
- confirmed `rift.vst3` bundle output and install
- confirmed `drumgen.vst3` bundle output and install
- confirmed `drumkit.vst3` bundle output and install
- confirmed `cadence.vst3` bundle output and install
- confirmed `counterpointer.vst3` bundle output and install
- confirmed `gremlin.vst3` bundle output and install
- confirmed `gremlin_driver.vst3` bundle output and install
- confirmed `ground.vst3` bundle output and install
- confirmed `floozy.vst3` bundle output and install
- confirmed `basilico.vst3` bundle output and install
- confirmed `canticle.vst3` bundle output and install
- confirmed `luma.vst3` bundle output and install
- confirmed `paunchlad.vst3` bundle output and install
- confirmed `lifeform.vst3` bundle output and install
