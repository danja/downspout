# Cross-platform release builds

Linux remains the baseline release build. macOS and Windows are now configured
as best-effort release package builds while their host validation is still
being proven.

## Current status

- Linux `linux-x86_64` builds and packages on `ubuntu-24.04`.
- macOS now builds a single `macos-universal` package on `macos-15` with
  `CMAKE_OSX_ARCHITECTURES=x86_64;arm64` and
  `CMAKE_OSX_DEPLOYMENT_TARGET=10.15`. This path is wired in CI/release, but
  still needs confirmation on GitHub Actions and a DAW/plugin scan on macOS.
- Windows now builds `windows-x86_64` from `ubuntu-22.04` with MinGW rather
  than native `windows-2022`/MSVC. The toolchain file is
  `cmake/toolchains/mingw-x86_64.cmake`.
- Windows package builds currently set `DOWNSPOUT_RUN_TESTS=0`,
  `DOWNSPOUT_STRIP=0`, and `DOWNSPOUT_BUILD_JOBS=2`. Tests are disabled
  because this is a cross build; parallelism is capped because full parallel
  VST3 linking was too opaque and slow locally.
- A local Windows package smoke succeeded on Linux after installing MinGW:
  `/tmp/downspout-win-package-dist/downspout-ci-local-windows-x86_64-vst3.zip`.
  The archive contained the expected `Contents/x86_64-win/*.vst3` layout for
  the release plugin set.
- `release.yml` marks macOS and Windows as `continue-on-error`. Linux remains
  the required release asset; non-Linux packages are included when they build.

## Local Windows build requirements

Install the Windows cross-build toolchain with:

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  binutils-mingw-w64-x86-64 \
  g++-mingw-w64-x86-64 \
  mingw-w64 \
  wine
```

`wine` is not required for the current package build because tests are skipped,
but it will be needed when cross-built test execution or validation is enabled.

## Useful references

Local references:

- `/home/danny/github/dpf-makefile-action`
- `/home/danny/github/PawPaw`

Upstream sources:

- https://github.com/DISTRHO/dpf-makefile-action
- https://github.com/DISTRHO/PawPaw

Relevant observations from those references:

- DPF's reference Windows builds use Linux runners with MinGW/Wine, not native
  Windows/MSVC runners.
- DPF's reference macOS builds use explicit architecture and deployment target
  settings rather than relying on runner defaults.
- PawPaw is still useful if future plugin dependencies need cross-built support
  beyond the current DPF-only dependency set.

## Next actions

1. Run the updated CI workflow on GitHub and inspect the `macos-universal` and
   `windows-x86_64` artifacts.
2. If the Windows GitHub runner succeeds, keep the MinGW path and consider
   making Windows non-experimental after at least one tagged release produces a
   usable package.
3. Validate the Windows VST3 package in a Windows DAW or plugin scanner. The
   build artifact exists locally, but runtime host loading has not been proven.
4. Validate the macOS universal package on both Apple Silicon and Intel hosts,
   or at least with `file`/`lipo -info` plus a plugin scan on one macOS machine.
5. Re-enable some Windows validation later by either setting
   `CMAKE_CROSSCOMPILING_EMULATOR=wine` for selected core tests or adding a
   separate lightweight Wine smoke test. Keep package generation separate from
   full validation until it is stable.
6. If Windows CI remains slow, add a package-only CMake target or disable core
   test executable targets for cross-package builds so `cmake --build` does not
   compile unused `.exe` tests.
7. If macOS fails on GitHub Actions, compare against
   `dpf-makefile-action`'s macOS setup: Homebrew tool installation/removal,
   deployment target flags, and DPF bundle packaging expectations.
