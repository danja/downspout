# downspout plan

## Goal

`downspout` is a clean repository for porting selected `flues` LV2 plugins into more widely usable native plugin formats, with VST3 as the first practical target.

The motivation is straightforward:

- the existing `flues` LV2 plugins already contain promising DSP and host-interaction work;
- LV2 adoption is limited across mainstream DAWs;
- local development has been Linux-centric, which reduces real-world feedback from other producers.

The project should therefore focus on preserving proven behavior while re-hosting that behavior in a portable plugin framework.

## Initial targets

The first ports will be based on:

- `~/github/flues/lv2/bassgen`
- `~/github/flues/lv2/p-mix`

These two plugins were chosen deliberately because together they cover the main migration risks:

- `bassgen` exercises MIDI generation, transport sync, state persistence, and control/UI messaging.
- `p-mix` exercises audio processing, transport-driven behavior, state, and multi-channel routing.

If these ports succeed, the repository will have a workable pattern for both MIDI-producing and audio-processing plugins.

## Framework direction

Current direction is to use DPF as the plugin framework.

Reasoning:

- DPF supports VST3, LV2, CLAP, and standalone builds from one codebase.
- DPF exposes host time-position support, which is essential for both `bassgen` and `p-mix`.
- DPF supports plugin state and custom UI paths, which are both relevant to the selected targets.

Important constraint:

- DPF is the integration layer, not the architecture.
- Shared DSP and musical logic should live outside framework entry points so ports remain testable and maintainable.

## Confirmed requirements

As of 2026-04-18, the working requirements are:

### Functional

- Preserve the musical and transport behavior of the source LV2 plugins before adding new features.
- Support host transport/time information robustly enough for bar-based and loop-aware behavior.
- Support saved/restored plugin state.
- Support custom UI, but do not let UI decisions block DSP/core migration.
- Keep per-plugin metadata, parameters, defaults, and ranges traceable back to source implementations.

### Structural

- Use a multi-plugin repository layout.
- Separate shared reusable code from plugin-specific wrappers.
- Separate framework-neutral logic from DPF-specific glue.
- Keep build files modular so incomplete plugins do not break the whole tree.

### Quality

- Add automated tests for deterministic logic.
- Add explicit regression coverage for transport edge cases.
- Keep documentation short, technical, and current.

## Migration strategy

Each plugin port should follow this order:

1. Audit the source LV2 plugin and identify host-neutral logic.
2. Extract deterministic core logic into plain C++ classes with tests.
3. Define parameter/state contracts for the new plugin.
4. Add DPF wrapper code for DSP and transport access.
5. Add UI wrapper code after the core behavior is stable.
6. Compare behavior against the source plugin in a host.

This order matters. The mistake to avoid is porting LV2 structure directly into DPF without first isolating what is actually plugin logic versus host plumbing.

## Proposed repository layout

```text
downspout/
├── AGENTS.md
├── CMakeLists.txt
├── README.md
├── cmake/
├── docs/
│   ├── plan.md
│   └── requirements.md
├── include/
│   └── downspout/
├── plugins/
│   ├── bassgen/
│   │   ├── CMakeLists.txt
│   │   ├── docs/
│   │   ├── include/
│   │   └── src/
│   └── p-mix/
│       ├── CMakeLists.txt
│       ├── docs/
│       ├── include/
│       └── src/
├── src/
│   └── common/
├── tests/
└── third_party/
```

## DPF implications

DPF appears viable for this project, but a few implications need to guide implementation:

- plugin code should be written in C++, even when porting from mixed C/C++ LV2 code;
- framework-specific state and UI messaging must be kept thin and isolated;
- transport handling should be validated carefully because both target plugins depend on host timing;
- DPF can support multiple output formats later, but the first milestone should target one format cleanly rather than many formats poorly.

## Immediate tasks

The first concrete tasks for `downspout` are:

1. Maintain this plan as the canonical root project brief.
2. Add `AGENTS.md` with repository-specific working rules.
3. Create a multi-plugin scaffold that can grow without forcing immediate DPF integration.
4. Write a requirements document that turns the plan into implementation constraints.
5. Begin the `bassgen` audit and identify its reusable core modules.

Progress as of 2026-04-18:

- root planning and requirements documents exist;
- repository rules and scaffold exist;
- `bassgen` has a portable core library with deterministic tests;
- `bassgen` now builds as a VST3 bundle with UI via vendored DPF;
- `p-mix` now builds as a first VST3 wrapper with UI via vendored DPF;
- `install.sh` exists as the intended build/install entrypoint for local VST deployment.

Current main gap:

- DPF is now vendored and the first `bassgen` wrapper target builds successfully.
- `install.sh` now installs real `bassgen.vst3` and `p_mix.vst3` bundles.
- the main remaining gaps are host validation of `bassgen`, host validation of `p-mix`, and validating the first tagged GitHub Actions release.

## Next implementation sequence

The next work should proceed in this order:

1. Continue light host validation of `bassgen.vst3` in Reaper until there are no obvious wrapper/UI regressions.
2. Continue validating `p-mix.vst3` in Reaper, especially transport sync, multichannel routing, and the new UI.
3. Tighten any remaining `p-mix` layout or interaction issues discovered in host testing.
4. Validate the release-build workflow on the first public tag so installable bundles can be built reproducibly in `Release` mode.
5. Confirm `install.sh` and local docs against clean `Release` installs for both current plugins.
6. Use the wrapper patterns from `bassgen` and `p-mix` to choose the third plugin target.

Reasoning:

- `bassgen` is already far enough along that the remaining work is validation and incremental fixes, not architecture.
- `p-mix` already has portable engine and test coverage, so wrapper integration is now the highest-value missing deliverable.
- release builds need to become a first-class workflow before the repository is ready for broader use beyond local iteration.

## Non-goals for the first phase

- No attempt to port every `flues` plugin immediately.
- No premature UI redesign.
- No hard dependency on a single DAW for validation.
- No copy-paste port that keeps LV2 assumptions embedded in the core logic.
