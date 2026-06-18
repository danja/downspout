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

Original follow-on plugin idea:

- `counterpointer`: transport-aware MIDI processor that learns the incoming MIDI
  pattern, like `cadence`, and emits a complementary monophonic counter-melody.

These two plugins were chosen deliberately because together they cover the main migration risks:

- `bassgen` exercises MIDI generation, transport sync, state persistence, and control/UI messaging.
- `p-mix` exercises audio processing, transport-driven behavior, state, and host channel-layout decisions.

Those ports established the project pattern now used by the wider plugin set:
portable core, deterministic tests, thin DPF wrapper, custom UI, and local VST3
install packaging.

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
- Evolve transport-aware generators toward true meter-aware behavior where the
  musical goal requires it, especially for compound meters and grouped pulse
  feel.
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

## Current status

The initial scaffold and first-port work are complete enough that the project
now has a stable implementation pattern: portable core, deterministic tests,
thin DPF wrapper, custom UI, local install script, and release packaging.

Progress as of 2026-05-27:

- root planning and requirements documents exist;
- repository rules and scaffold exist;
- `bassgen` has a portable core library with deterministic tests;
- `bassgen` now builds as a VST3 bundle with UI via vendored DPF;
- `p-mix` now builds as a first VST3 wrapper with UI via vendored DPF;
- `e-mix` now has a portable core library, deterministic tests, and a first VST3 wrapper target with a redesigned UI via vendored DPF;
- `m-mix` now has a portable MIDI-gate core, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `melgen` now has a phrase-aware MIDI melody core, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `rift` now exists as an original `downspout` transport-aware live/sample buffer effect with WAV loading, beat-mapped sample playback, a portable core, deterministic tests, and a VST3 wrapper target with UI via vendored DPF;
- `drumgen` now has a portable core library, a host-neutral MIDI engine, text serialization helpers, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `drumkit` now ports the `flues` drum synth through a portable core and a first VST3 instrument wrapper with UI via vendored DPF;
- `cadence` now has a portable core library, a host-neutral learning/playback engine, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `counterpointer` now has a portable core, deterministic tests, text state
  serialization, and a first DPF/VST3 wrapper with custom UI;
- `sidecar` now exists as an original AI-ready MIDI phrase player with local
  phrase validation, deterministic fallback generation, text state
  serialization, transport-aware playback, deterministic tests, and a first
  DPF/VST3 wrapper with custom UI;
- `gremlin` now has a portable core library, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `gremlin-driver` now has a portable MIDI modulation core, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `ground` now exists as an original long-form MIDI bass generator with a portable form-planning core, explicit Dub and Jazz styles, guarded bass-register output, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `floozy` now ports `flues/lv2/floozy-poly` as a corrected 8-voice hybrid physical/modulation synth with a portable core, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `basilico` now exists as an original monophonic bass instrument with upright, electric, dub, acid, and industrial models, tempo-aware wobble modulation, acid squelch, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `canticle` now exists as an original 12-voice polyphonic tonal instrument for keys, reed, pad, pluck, and glass roles, with deterministic tests and a first VST3 wrapper target with UI via vendored DPF;
- `luma` now exists as an original Launchpad-oriented MIDI performance generator with pad agents, LED feedback, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `paunchlad` now exists as an original Launchpad-oriented dub performance effect with echo throws, sirens, spring splashes, dropouts, chops, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `lifeform` now exists as an original Launchpad-oriented Conway Game of Life MIDI generator with one generation per beat, LED feedback, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `install.sh` exists as the intended build/install entrypoint for local VST deployment.

Current main gap:

- DPF is now vendored and all current wrapper targets build successfully.
- `install.sh` now installs real `bassgen.vst3`, `p_mix.vst3`, `e_mix.vst3`, `m_mix.vst3`, `melgen.vst3`, `rift.vst3`, `drumgen.vst3`, `drumkit.vst3`, `cadence.vst3`, `counterpointer.vst3`, `sidecar.vst3`, `gremlin.vst3`, `gremlin_driver.vst3`, `ground.vst3`, `floozy.vst3`, `basilico.vst3`, `canticle.vst3`, `luma.vst3`, `paunchlad.vst3`, and `lifeform.vst3` bundles.
- `scripts/package-release.sh` still needs an explicit release-glue update before
  `sidecar.vst3` is included in public release archives.
- `bassgen` now has a richer Jazz model with ii-V-I-turnaround roles, dominant color, chord-tone targeting, approaches/enclosures, and a general `Color` control.
- the main remaining gaps are host validation across the full plugin set, validating the first tagged GitHub Actions release, adding the Sidecar coordinator/OpenAI workflow, and pushing the shared meter/style/color model further up the musical stack where it improves generator behavior.

## Meter direction

The next architectural addition was shared meter handling.

Reasoning:

- several wrappers already followed host bar timing successfully;
- `ground` and `drumgen` needed a structural refactor away from fixed `4/4`
  bar math;
- `bassgen` is more adaptable, but its rhythmic language is still simple-meter
  oriented;
- compound-meter work is necessary if `downspout` is going to support use
  cases such as generative Irish folk material.

See [docs/meter.md](meter.md) for the concrete design target and plugin impact.

## Next implementation sequence

The next work should proceed in this order:

1. Continue host validation of the current plugin set in Reaper and fix any DAW-facing issues that block basic use.
2. Add folk-oriented style vocabulary where musically justified: reel, jig, slip jig, hornpipe, polka, drone/modal accompaniment.
3. Add pickup/anacrusis handling and phrase-end behavior where the generator concept needs it.
4. Validate the release-build workflow on the first public tag so installable bundles can be built reproducibly on GitHub, not just locally.

Reasoning:

- the current plugin set already follows the portable-core-plus-wrapper pattern, so the highest-value work has shifted to host behavior, routing validation, UI clarity, and release packaging.
- release builds need to become a first-class workflow before the repository is ready for broader use beyond local iteration.
- DAW validation exposed a broader architectural gap: transport sync existed, but true meter support did not exist consistently across the generator plugins.
- the shared meter abstraction now exists and is wired into the transport layer plus the `bassgen`, `ground`, and `drumgen` generators.

## Non-goals for the first phase

- No attempt to port every `flues` plugin immediately.
- No premature UI redesign.
- No hard dependency on a single DAW for validation.
- No copy-paste port that keeps LV2 assumptions embedded in the core logic.
