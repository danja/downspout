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
  Intended for a future external or vendored DPF location.
- `DOWNSPOUT_RUN_TESTS`
  Set to `0` to skip `ctest` during install.

## Release builds

Release builds are now an explicit project requirement.

Current default behavior already configures with `CMAKE_BUILD_TYPE=Release`, but the broader release workflow still needs follow-up work:

- define expected release artifacts clearly;
- verify release-mode bundles in host testing;
- decide whether release packaging should stay in `install.sh` or move to a dedicated release script.

## Current outputs

The repository currently installs two real wrapper targets:

- `bassgen.vst3` with UI
- `p_mix.vst3` without a custom UI yet

The next install-related validation is host-side confirmation that both bundles behave correctly in `Release` builds.

## Verified behavior

The script has been smoke-tested with a temporary install root under `/tmp`:

- configure
- build
- test
- install
- confirmed `bassgen.vst3` bundle output and install
- confirmed `p_mix.vst3` bundle output and install
