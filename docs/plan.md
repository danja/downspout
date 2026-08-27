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
- `t-mix` now exists as an original eight-input stereo mixer with a portable
  summing core, constant-power pan, mute/solo behavior, pre-fader meters,
  deterministic tests, and a DPF/VST3 wrapper with a mixer-style UI; it now
  accepts sample-accurate, click-smoothed CC 20-27 producer-gain overlays while
  preserving manual faders as the saved mix balance;
- `mixgen` now exists as an original automatic producer for `t-mix`, with
  repeatable random, low-discrepancy quasi-random, and Euclidean eight-lane
  gain patterns, transport synchronization, live lane status, deterministic
  tests, and a focused DPF/VST3 UI;
- `loopdelay` now exists as a stereo delay and capture looper intended after
  `t-mix`, with free or BBT-derived time, feedback/ping-pong/overdub shaping,
  sample-accurate fixed CC 30/31 producer control, transient MIDI takeover,
  deterministic transport tests, and a task-oriented DPF/VST3 UI;
- `lightverb` now exists as a low-CPU stereo reverb for the Transmission mix
  path, using a fixed-cost four-line feedback-delay network, exact dry and
  100%-wet send operation, non-conflicting CC 32/33 producer control,
  deterministic safety/allocation tests, and a focused DPF/VST3 UI;
- Producer Control Bus v1 now loosely coordinates Mixgen, T-Mix, Loopdelay, and
  Lightverb through CC 19 lifecycle, CC 20–33 payloads, per-chain channel
  filtering, optional ownership gates, transparent MIDI-through, configurable
  Mixgen FX macro routing, and an end-to-end portable contract test;
- `melgen` now has a phrase-aware MIDI melody core, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `rift` now exists as an original `downspout` transport-aware live/sample buffer effect with WAV loading, beat-mapped sample playback, a portable core, deterministic tests, and a VST3 wrapper target with UI via vendored DPF;
- `drumgen` now has a portable core library, a host-neutral MIDI engine, text serialization helpers, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `drumkit` now ports the `flues` drum synth through a portable core and a first VST3 instrument wrapper with UI via vendored DPF;
- `cadence` now has a portable core library, a host-neutral learning/playback engine, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `arpgen` now exists as an original transport-synced MIDI arpeggiator with
  meter-relative chord capture, scale-derived material, deterministic tests,
  and a DPF/VST3 wrapper with custom UI;
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
- `xoxolo` now exists as an original simple MIDI drum pattern editor with a
  fixed 11-lane drumkit map, 32-step maximum, text state serialization,
  deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `tuney-vst` now ports Tuney 0.3.39's text-to-music core as a focused-typing
  instrument/MIDI generator with portable mapping, tuning, free-time
  scheduling, synthesis, versioned text state, deterministic tests, and a DPF UI;
- `campione` now exists as an original multi-zone sampler instrument with per-note MIDI mapping, WAV file loading, audio recording with auto-pitch detection, pitch-shift gap fill, zero-crossing loop snap, crossfade looping, deterministic core, and a first VST3 wrapper target with UI via vendored DPF;
- `install.sh` exists as the intended build/install entrypoint for local VST deployment.

Current main gap:

- DPF is now vendored and all current wrapper targets build successfully.
- `install.sh` and `scripts/package-release.sh` now install and package real
  `bassgen.vst3`, `p_mix.vst3`, `e_mix.vst3`, `m_mix.vst3`, `t_mix.vst3`,
  `mixgen.vst3`, `loopdelay.vst3`, `lightverb.vst3`, `melgen.vst3`,
  `rift.vst3`, `orchid.vst3`, `ambo.vst3`, `drumgen.vst3`, `drumkit.vst3`,
  `cadence.vst3`, `arpgen.vst3`, `counterpointer.vst3`, `sidecar.vst3`, `gremlin.vst3`,
  `gremlin_driver.vst3`, `ground.vst3`, `floozy.vst3`, `basilico.vst3`,
  `canticle.vst3`, `luma.vst3`, `paunchlad.vst3`, `lifeform.vst3`, and
  `xoxolo.vst3`, `syrinx.vst3`, `tuney_vst.vst3`, `harmonic_atlas.vst3`,
  `conductor.vst3`, `drift.vst3`, `mnemosyne.vst3`, `polymeter.vst3`,
  `oracle.vst3`, `mosaic.vst3`, `resonance_garden.vst3`, `orbit.vst3`,
  `guardian.vst3`, `campione.vst3`, and `skream.vst3` bundles.
- `bassgen` now has a richer Jazz model with ii-V-I-turnaround roles, dominant color, chord-tone targeting, approaches/enclosures, and a general `Color` control.
- the main remaining gaps are host validation across the full plugin set,
  validating the expanded release payload, and deeper interaction testing of
  the generative-workstation suite in Transmission.

## Generative-workstation UI readiness

The first implementation of Harmonic Atlas, Conductor, Drift, Mnemosyne,
Polymeter, Oracle, Mosaic, Resonance Garden, Orbit, and Guardian proved the
portable cores and host wrappers, but rendered every parameter through the same
anonymous slider grid. That made choice values, switches, MIDI routing,
processor status, and musical relationships difficult to understand in
practice.

The approved UI-readiness pass for these ten plugins is:

1. Replace numeric enum and boolean sliders with named choices, switches, and
   action buttons. Display musical notes, pitch classes, MIDI channels, CC
   destinations, beat/bar units, percentages, milliseconds, and decibels in
   domain terms.
2. Use explicit workflow sections and separate read-only processor feedback
   from editable controls. Provide default reset gestures, concise contextual
   help, and strong disabled-state treatment where a control is not currently
   relevant.
3. Give Harmonic Atlas a current-chord/keyboard view; Conductor a section
   timeline; Mnemosyne a reservoir view; Oracle an analysis dashboard;
   Resonance Garden a resonator-energy view; Orbit a trajectory view; and
   Guardian conventional safety meters and diagnostic lamps.
4. Give Drift and Polymeter consistent four-lane layouts with per-lane mode or
   pattern previews rather than splitting lanes across unrelated columns.
5. Give Mosaic a complete four-slot sample workflow with visible load,
   replace, clear, filename, and error/empty status controls. File loading
   remains on the DPF state/control path and never occurs in audio processing.
6. Rebuild the ten core tests and VST3 targets, validate the bundles, capture
   and inspect updated catalogue screenshots, and document a practical host
   test recipe before considering the suite UI-ready.

Per-plugin usability targets:

- **Harmonic Atlas:** named movement/voicing choices, musical pitch-class
  labels, clear routing, and a live root/keyboard view.
- **Conductor:** a visible section timeline, named form modes, coherent section
  weights, and separately grouped advanced MIDI commands.
- **Drift:** four complete lane cards with named sources, shape previews,
  musical timing, bounded ranges, and readable CC destinations.
- **Mnemosyne:** visible reservoir occupancy, named modes/transforms,
  capture guidance, and an explicit clear-memory workflow.
- **Polymeter:** one coherent view per lane with Euclidean pattern/playhead,
  note names, and primary rhythm controls kept together.
- **Oracle:** separate listening, response, and routing workflows with live
  analysis meters and musical response boundaries.
- **Mosaic:** four visible sample slots with load/replace/clear actions,
  filenames, pool status, and a slice/grain visualization.
- **Resonance Garden:** grouped resonance/tuning/output controls and a live
  eight-voice pitch/energy view.
- **Orbit:** named trajectories, musical timing, contextual seed control, and
  a live path/position view.
- **Guardian:** conventional limiter meters, protection switches, neutral
  diagnostics, reported look-ahead, and a real diagnostic-reset button.

The shared implementation remains local to `plugins/generative-common`; it is
not a replacement for established plugin UIs elsewhere in the repository.
The practical host checklist is maintained in
[docs/generative-suite-test.md](generative-suite-test.md).

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
