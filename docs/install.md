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

The repository currently installs three real wrapper targets:

- `bassgen.vst3` with UI
- `p_mix.vst3` with UI
- `drumgen.vst3` with host generic parameter UI

The next install-related validation is host-side confirmation that all three bundles behave correctly in `Release` builds.

## Verified behavior

The script has been smoke-tested with a temporary install root under `/tmp`:

- configure
- build
- test
- install
- confirmed `bassgen.vst3` bundle output and install
- confirmed `p_mix.vst3` bundle output and install
- confirmed `drumgen.vst3` bundle output and install
