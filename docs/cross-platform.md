# Cross-platform release builds

Linux remains the baseline release build. macOS and Windows are now configured
as best-effort release package builds while their host validation is still
being proven.

## Current status

- Linux `linux-x86_64` builds and packages on `ubuntu-24.04`.
- macOS now uses `distrho/dpf-cmake-action@v1` with its explicit universal
  `macos-10.15` target on `macos-15`. The action supplies the architecture,
  deployment-target, and DPF-specific setup; downspout then repackages its
  `bin/*.vst3` output into the normal zip/checksum format.
- Windows now uses the same action’s `win64` target from `ubuntu-22.04`. This
  uses the DPF-tested MinGW/Wine cross-build setup rather than the repository’s
  generic CMake toolchain path. The generated bundles are repackaged by
  `scripts/package-built-bundles.sh`.
- The DPF action’s non-Linux package jobs disable `BUILD_TESTING` and Sidecar:
  tests are not useful during a cross-package build, and Sidecar has additional
  Linux-only runtime dependencies.
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

- `/home/danny/github/dpf-cmake-action`
- `/home/danny/github/PawPaw`

Upstream sources:

- https://github.com/DISTRHO/dpf-cmake-action
- https://github.com/DISTRHO/PawPaw

Relevant observations from those references:

- DPF's reference Windows builds use Linux runners with MinGW/Wine, not native
  Windows/MSVC runners; the action also sets the compiler-ar/randlib and VST3
  architecture values that the old generic path omitted.
- DPF's reference macOS builds use explicit architecture and deployment target
  settings and package the DPF bundle output on the macOS runner.
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
7. If macOS or Windows still fails on GitHub Actions, compare the failing step
   with the pinned DPF action release before changing downspout’s portable
   plugin code.
