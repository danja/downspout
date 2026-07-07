# New Plugin Checklist

## Directory shape

```text
plugins/<plugin>/
├── CMakeLists.txt
├── README.md
├── include/
├── src/
│   └── dpf/
├── tests/
└── docs/
```

Keep plugin-specific implementation in the plugin directory. Use `src/common/` or `include/downspout/` only with explicit user approval because existing plugins may rely on those APIs.

## CMake pattern

Plugin-local CMake usually has:

- `add_library(downspout_<id>_core STATIC ...)`
- `target_link_libraries(... downspout-project-options)`
- plugin-local include directories
- `dpf_add_plugin(...)` under `if(DOWNSPOUT_ENABLE_DPF)`
- `install(DIRECTORY "${PROJECT_BINARY_DIR}/bin/<bundle>.vst3" DESTINATION "." OPTIONAL)`
- `add_executable(downspout_<id>_core_tests ...)` under `if(BUILD_TESTING)`
- `add_test(NAME downspout_<id>_core_tests COMMAND downspout_<id>_core_tests)`

Root CMake adds:

- `option(DOWNSPOUT_BUILD_<PLUGIN_ID> "Configure the <plugin> ..." ON)`
- gated `add_subdirectory(plugins/<plugin>)`

## Source pattern

For a DPF plugin, include:

- `src/dpf/DistrhoPluginInfo.h`
- `src/dpf/<Plugin>Plugin.cpp`
- `src/dpf/<Plugin>UI.cpp` when custom UI is in scope

For stateful plugins, define a stable text serialization contract before UI work.

For transport-aware plugins, test stopped transport, loop boundaries, rewind, tempo/bar changes, and play-start behavior.

For controller-heavy plugins, expose output status parameters so the UI reflects effective processor state rather than only host automation values.

## Documentation

Plugin-local docs should record porting assumptions and mappings. Keep root docs concise and update only bundle lists, status, or process docs that actually changed.

## Validation

At minimum run:

```bash
cmake --build build --target downspout_<id>_core_tests
./build/plugins/<plugin>/downspout_<id>_core_tests
cmake --build build --target <plugin-target>
bash -n install.sh
bash -n scripts/package-release.sh
```

Also run a static consistency check that package-release expected bundles match plugin CMake install declarations. If screenshots/catalog changed, run or explicitly defer screenshot generation.
