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
- `p-mix` exercises audio processing, transport-driven behavior, state, and host channel-layout decisions.

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

Progress as of 2026-04-24:

- root planning and requirements documents exist;
- repository rules and scaffold exist;
- `bassgen` has a portable core library with deterministic tests;
- `bassgen` now builds as a VST3 bundle with UI via vendored DPF;
- `p-mix` now builds as a first VST3 wrapper with UI via vendored DPF;
- `e-mix` now has a portable core library, deterministic tests, and a first VST3 wrapper target with a redesigned UI via vendored DPF;
- `rift` now exists as an original `downspout` transport-aware buffer effect with a portable core, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `drumgen` now has a portable core library, a host-neutral MIDI engine, text serialization helpers, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `cadence` now has a portable core library, a host-neutral learning/playback engine, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `gremlin` now has a portable core library, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `gremlin-driver` now has a portable MIDI modulation core, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `install.sh` exists as the intended build/install entrypoint for local VST deployment.

Current main gap:

- DPF is now vendored and all current wrapper targets build successfully.
- `install.sh` now installs real `bassgen.vst3`, `p_mix.vst3`, `e_mix.vst3`, `rift.vst3`, `drumgen.vst3`, `cadence.vst3`, `gremlin.vst3`, and `gremlin_driver.vst3` bundles.
- both `install.sh` and `scripts/package-release.sh` now pass clean full-tree smoke runs for all eight bundles.
- the main remaining gaps are host validation of `bassgen`, host validation of `p-mix`, host validation of `e-mix`, host validation of `rift`, host validation of `drumgen`, host validation of `cadence`, host validation of `gremlin`, host validation of `gremlin-driver`, and validating the first tagged GitHub Actions release.

## Next implementation sequence

The next work should proceed in this order:

1. Continue light host validation of `bassgen.vst3` in Reaper until there are no obvious wrapper/UI regressions.
2. Continue validating `p-mix.vst3` in Reaper, especially transport sync, multichannel routing, and the new UI.
3. Validate `e_mix.vst3` in Reaper, especially stopped transport behavior, restart handling, and whether the redesigned UI actually clarifies the cycle.
4. Validate `rift.vst3` in Reaper, especially whether the macro layer, preview lane, and performance buttons make the effect usable without extra explanation.
5. Tighten any remaining `p-mix` layout or interaction issues discovered in host testing.
6. Validate `cadence.vst3` in Reaper, especially learning, restart/rewind handling, and saved-state restore.
7. Validate `gremlin.vst3` in Reaper, especially combined note/controller MIDI, scene switching, and level range.
8. Validate `gremlin_driver.vst3` in Reaper, especially MIDI pass-through, transport sync, and chaining into `gremlin.vst3`.
9. Validate `drumgen.vst3` in Reaper, especially the new action-button UI, transport sync, MIDI output behavior, and state restore through the host.
10. Validate the release-build workflow on the first public tag so installable bundles can be built reproducibly on GitHub, not just locally.

Reasoning:

- `bassgen` is already far enough along that the remaining work is validation and incremental fixes, not architecture.
- `p-mix` already has portable engine and test coverage, so wrapper integration is now the highest-value missing deliverable.
- `e-mix` now follows the same portable-core-plus-wrapper pattern, so the next useful work is host feedback on the redesigned UI rather than more extraction.
- `rift` follows the same pattern but as an original effect, so its main risk is not extraction fidelity but whether the UI actually makes the concept learnable in a DAW.
- release builds need to become a first-class workflow before the repository is ready for broader use beyond local iteration.
- `drumgen` has now reached the same core-plus-wrapper milestone as `bassgen`, so the remaining work is validation, polish, and bug-fixing rather than architectural extraction.
- `cadence` has now reached the same core-plus-wrapper milestone, so the next useful work is host validation and incremental fixes rather than more architectural churn.
- `gremlin` and `gremlin-driver` now reach the same milestone, so the highest-value work has shifted to host behavior, routing validation, and release packaging rather than more extraction.

## Non-goals for the first phase

- No attempt to port every `flues` plugin immediately.
- No premature UI redesign.
- No hard dependency on a single DAW for validation.
- No copy-paste port that keeps LV2 assumptions embedded in the core logic.
