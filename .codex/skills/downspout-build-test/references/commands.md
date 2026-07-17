# Build And Test Commands

## Discovery

```bash
find . -maxdepth 2 -name CMakeCache.txt -print
rg -n "add_executable\(downspout_.*_core_tests|add_test\(|dpf_add_plugin|install\(DIRECTORY" plugins/<plugin>/CMakeLists.txt
cmake --build build --target help
```

Do not use `git` commands unless the user explicitly approves them.

## Common target forms

- Core library: `downspout_<plugin_id>_core`
- Core tests: `downspout_<plugin_id>_core_tests`
- DPF target: often the plugin slug or underscore bundle name; verify in CMake output or `--target help`.
- Bundle: `${PROJECT_BINARY_DIR}/bin/<bundle>.vst3`

Examples:

- `rift`: `downspout_rift_core_tests`, bundle target `rift`, bundle `rift.vst3`.
- `p-mix`: `downspout_p_mix_core_tests`, bundle `p_mix.vst3`.
- `gremlin-driver`: `downspout_gremlin_driver_core_tests`, bundle `gremlin_driver.vst3`.

## Narrow commands

```bash
cmake --build build --target downspout_rift_core_tests
./build/plugins/rift/downspout_rift_core_tests
cmake --build build --target rift
ctest --test-dir build -R downspout_rift_core_tests --output-on-failure
```

Replace `rift` with the current plugin and target names.

## Broader commands

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Use these for shared-code changes, root CMake changes, or changes that affect multiple plugins.

## Script checks

For shell scripts changed in `install.sh` or `scripts/*.sh`:

```bash
bash -n install.sh
bash -n scripts/package-release.sh
bash -n scripts/capture-plugin-screenshots.sh
```

If `actionlint` is available and workflows changed:

```bash
actionlint
```

## VST3 validator

```bash
/home/danny/tools/vst3sdk-build/bin/Release/validator \
  build/bin/<bundle>.vst3 > /tmp/<bundle>-validator.log 2>&1
```

Check both the process exit status and the log's final result. For a host scan
failure, compare the same validator version against a known-working installed
bundle such as `~/.vst3/gremlin.vst3`.

## Interpreting failures

- Missing `CMakeCache.txt`: configure first, then build.
- DPF/UI compile errors: inspect `src/dpf/*Plugin.cpp`, `src/dpf/*UI.cpp`, parameter enum counts, and generated target names.
- Serialization test failures: check defaults for missing fields and clamp logic.
- Transport test failures: check block serial calculation, loop/rewind handling, and stopped-transport pass-through semantics.
