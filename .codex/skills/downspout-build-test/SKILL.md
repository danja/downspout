---
name: downspout-build-test
description: Build, test, and verify downspout plugins and core targets. Use when Codex needs to configure CMake, build a plugin or test target, run CTest/core tests, choose narrow verification for a change, or avoid known downspout build-directory races.
---

# Downspout Build Test

Use this skill whenever validating downspout code changes.

## Build-directory rule

On a fresh CMake build directory, do not start `cmake --build` until configure has fully completed. This repo can race cache generation and fail with `Error: could not load cache` if build starts too early.

Before building, check whether the target build directory has `CMakeCache.txt`:

```bash
find build -maxdepth 1 -name CMakeCache.txt -print
```

If no cache exists, configure first and wait for success:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
```

## Fast plugin verification

For a plugin change, usually run:

```bash
cmake --build build --target downspout_<plugin_id>_core_tests
./build/plugins/<plugin>/downspout_<plugin_id>_core_tests
cmake --build build --target <bundle_target>
ctest --test-dir build -R downspout_<plugin_id>_core_tests --output-on-failure
```

Use the plugin-local `CMakeLists.txt` to confirm exact target names. Hyphenated plugin names often use underscores in C++/test/bundle names, for example `p-mix` has `downspout_p_mix_core_tests` and `p_mix.vst3`.

## Verification scope

- Core-only logic: build and run the plugin core test executable.
- DPF wrapper or UI changes: also build the plugin target.
- Build/install/release script changes: run shell syntax checks and the affected script in the narrowest safe mode.
- Screenshot or Pages changes: use the screenshot/catalog workflow skill.
- Shared code changes: broaden tests to all impacted plugins.

## VST3 validation

When a VST3 bundle is rejected by a host, run Steinberg's command-line
`validator` against the built or installed bundle. Compare with a known-working
Downspout bundle when the output might reflect a DPF-wide limitation.

The locally installed validator is currently:

```bash
/home/danny/tools/vst3sdk-build/bin/Release/validator /path/to/plugin.vst3
```

Treat a signal, non-zero exit status, sanitizer report, or failed test as a
failure even if earlier tests are marked `Succeeded`. Validator output can be
very large for DPF MIDI plugins, so capture it to a temporary log and search for
`Failed`, `invalid`, `AddressSanitizer`, and the final `Result:` line.

Read [references/commands.md](references/commands.md) for target naming, useful commands, and validation heuristics.
