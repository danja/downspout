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

## Current limitation

The repository currently installs one real wrapper target:

- `bassgen.vst3`

Other plugins may still build only as portable cores until their DPF wrappers are added.

## Verified behavior

The script has been smoke-tested with a temporary install root under `/tmp`:

- configure
- build
- test
- install
- confirmed `bassgen.vst3` bundle output and install
