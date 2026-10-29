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
- `moka` now exists as an original stereo modal hit-object instrument (xylophone, glockenspiel, woodblock, glass bowl, metal sheet, tube) with ten controls, eleven factory presets, selectable 1–12 voice polyphony defaulting to 4, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF. Notes ramp in over the mallet contact time so no note opens with an impulse, and the panel plots the live partial ladder and ring time from the same table maths the engine renders;
- `luma` now exists as an original Launchpad-oriented MIDI performance generator with pad agents, LED feedback, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `paunchlad` now exists as an original Launchpad-oriented dub performance effect with echo throws, sirens, spring splashes, dropouts, chops, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `lifeform` now exists as an original Launchpad-oriented Conway Game of Life MIDI generator with one generation per beat, LED feedback, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `pratt` now ports the offline `pratt-synth` renderer to a real-time plugin built on
  Pratt polynomials (`f_n(2) = n`, `f_mn = f_m f_n`) and the stable all-pole filter
  `H_n(s) = n / f_n(2 + s/w0)`: eleven General MIDI voice families whose spectra come
  from `H_n` at `n = (note + 1) x base`, and a Pratt filter effect whose two index
  controls cascade as `H_m H_n = H_mn`, usable on its own, on audio, or after the
  synth. The engine matches the Python renderer's harmonics within 0.12 dB, has
  deterministic core/engine/state/threading suites (the last also run under
  ThreadSanitizer), a worker thread that pre-builds wavetables off the audio thread,
  and a VST3 wrapper with UI; host validation is pending;
- `retune` now exists as an original MIDI effect that retunes notes to a Scala `.scl` scale
  (cents and ratio lines, strict parser) by sending each held note on its own output
  channel (2-16) behind a per-note pitch bend, with oldest-note stealing; CC and program
  change are broadcast to the pool, poly aftertouch follows its note, and incoming pitch
  bend and channel aftertouch are dropped. It has a deterministic core suite (also run
  under ASan/UBSan), a VST3 wrapper with a scale map UI, and awaits host validation;
- `markov` now exists as an original Markov-chain melody generator: a random walk over the twelve
  pitch classes through a 12 x 12 transition matrix that is drawn on screen and can be edited, filled
  from one of eight styles, or learned from a melodic line on the MIDI input (first-order counts for
  every pair, second-order counts for every triple). The scale masks the matrix, Chaos reshapes it, and
  each phrase restarts the walk from the tonic so any position renders the same notes. It has a
  deterministic core suite (also run under ASan/UBSan) including locate and block-size independence,
  Conductor CC support, and a VST3 wrapper with a Magneto-look matrix editor; host validation is
  pending;
- `sprout` now exists as an original L-system MIDI generator: seven grammars (Plant,
  Koch, Dragon, Sierpinski, Cantor, Levy, Tree) are rewritten per generation, read
  as a turtle (F/G note, f rest, +/- degree, brackets branch) and played as a
  scale-quantised line with folding range, gate, seeded probability and branch-depth
  velocity. A Grow control adds one generation every N bars. It is stateless against
  the host transport, has a deterministic core suite (block-size independence, scale
  pins from `docs/scales.md`), and a VST3 wrapper with a pattern plot; host validation
  is pending;
- `plank` now reimplements Plinky's voice engine as a portable eight-voice
  synthesizer: a polyBLEP oscillator pair morphing into seventeen synthesised
  band-limited wavetables, Plinky's resonant two-pole filter, two envelopes,
  dynamic parameter smoothing, tape delay, shimmer reverb and mid/side width,
  played as one string per Launchpad grid column, with LED feedback,
  deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `xoxolo` now exists as an original simple MIDI drum pattern editor with a
  fixed 11-lane drumkit map, 32-step maximum, text state serialization,
  deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `tuney-vst` now ports Tuney 0.3.39's text-to-music core as a focused-typing
  instrument/MIDI generator with portable mapping, tuning, free-time
  scheduling, synthesis, versioned text state, deterministic tests, and a DPF UI;
- `campione` now exists as an original multi-zone sampler instrument with per-note MIDI mapping, WAV file loading, REX2 slice import (via VelociLoops), Serum/clm wavetable auto-detection, audio recording with auto-pitch detection, pitch-shift gap fill, zero-crossing loop snap, crossfade looping, deterministic core, and a first VST3 wrapper target with UI via vendored DPF;
- `beards` is a port of the Mutable Instruments Beads granular processor (grain engine,
  feedback, freeze, reverb, three storage qualities) with Drift CC 1-12 and Conductor CC
  21-24 control; the Beads delay mode and CV/gate inputs are not ported (see
  `plugins/beards/docs/design.md`). Host validation in REAPER remains.
- `trajectory` is a port of the Flues Trajectory polygon-bounce oscillator as an
  eight-voice VST3 instrument in the Magneto look: the oscillator and its six controls are
  kept (mapped to CC 1-6, aligned with Drift's default lane CCs), the reverb is dropped and
  the attack/release envelope is widened to ADSR. See `plugins/trajectory/docs/design.md`.
  Host validation in REAPER remains.
- `fathom` is a four-voice drone synthesiser in the Magneto look, modelled on the Vhikk X
  algorithm reference (a generator, a notch filter, then a delay network with a frequency
  shifter in its feedback loop). Five generator algorithms, two processor structures, Hold
  and glide for drones; CC 1-4 align with Drift's default lanes. See
  `plugins/fathom/docs/design.md`. Host validation in REAPER remains.
- `qune` is a stereo pitch quantiser in the Magneto look: a McLeod/NSDF tracker finds the
  input pitch and one of three shifters (a pitch-synchronous splice delay line, PSOLA, or a
  peak-locked phase vocoder with cepstral formants) moves it to the nearest note of a chosen
  scale or to incoming MIDI notes, with optional glide and formant preservation. Uses the
  canonical 24-scale ordering. No plugin state: every setting is a parameter. See
  `plugins/qune/docs/design.md`. Host validation in REAPER and listening tests on real
  voices remain.
- `grist` is a granular grain-delay effect in the Magneto look: a seven-second stereo history
  read by windowed grains (one per Window / Density, Hermite reads), each with a seeded random
  start, direction, pan, length and pitch (Pitch plus Range, optionally held to a scale), and a
  filtered, soft-limited feedback loop. The Window and loop Delay follow the host tempo. Original
  code. No plugin state: every
  setting is a parameter. See `plugins/grist/docs/design.md`. Host validation in REAPER and
  listening tests remain, and so does a decision on whether Resonance Garden needs a string
  mode to pair with it.
- `ladder` is a four-pole saturating low-pass ladder filter in the Magneto look, ported from the
  zero-delay-feedback ladder in `~/github/oxide-studio` (`oxide-dsp`, MIT/Apache-2.0) after probing it
  against the analytic response. Changes from the source: an exponential resonance taper, a Bass Comp
  control for the low end that feedback removes, and drive/mix/output. The panel plots a linear response
  model that the tests hold to the real filter. It adds a logarithmic option to `MagnetoKit` sliders.
  Ordinary parameters, no plugin state. See `plugins/ladder/docs/design.md`. Listening and host
  validation in REAPER remain; the rest of the oxide-studio candidates are listed in `TODO.md`.
- `monochord` is an eight-voice waveguide string in the Magneto look, a redesign inspired by `oxide-dsp`'s Tension device (`~/github/oxide-studio`, MIT/Apache-2.0), whose loop filters add
  delay that is never compensated (so the string plays flat, by an estimated 14 cents at middle C), whose hammer pulse is not zero-mean, and whose single dispersion all-pass is inaudible. Monochord tunes the
  loop from its own phase delay (error about 1e-12 samples; every measurable note within 0.4 cents), sets the fundamental's decay time exactly (within 12 %), makes stiffness a four-section cascade scaled to
  the period (on a low note partial 6 is +3 to +62 cents across 0.5-0.9), and adds a pickup comb whose nulls match theory, a body, a damper and a bow that sounds at every pressure and velocity. 14 automatable
  parameters, no plugin state, 134 core checks under ASan/UBSan. See `plugins/monochord/docs/design.md`. Listening and host validation in REAPER remain; one string per voice, no sympathetic strings.
- `nam` plays Neural Amp Modeler captures: a `.nam` model through the vendored NeuralAmpModelerCore (`third_party/NeuralAmpModelerCore`, MIT, with Eigen 3.4.0 headers) and a `.wav` cabinet IR through
  an own zero-latency partitioned FFT convolver, with input gain, noise gate, three-band tone, IR cuts, dry/wet and loudness normalisation. Models trained at another rate are resampled and the latency reported.
  Files are plugin state (paths), loaded on the main thread and handed over as shared rigs. 14 automatable parameters, core tests plus a manual DPF-wrapper probe. See `plugins/nam/docs/design.md`.
  Not yet auditioned in a host with real captures.
- `quartet` is an eight-voice four-operator FM synthesizer in the Magneto look, a redesign inspired by `oxide-dsp`'s Operator device (`~/github/oxide-studio`, MIT/Apache-2.0), whose full-level
  modulation is about 19 rad, whose "fine" moves a frequency by up to an octave, and whose PolyBLEP saw and square are wrong under phase modulation. Quartet has band-limited operator tables
  chosen by the modulated read speed, FM depth in cycles (sidebands match Bessel functions to four digits), fine in cents, algorithms as data (the panel draws them), optional 2x oversampling with a
  half-band decimator (flat to 18 kHz, -89 dB stopband), pitch envelope and SVF filter. 57 automatable parameters, no plugin state, 493 core checks under ASan/UBSan. See
  `plugins/quartet/docs/design.md`. Listening and host validation in REAPER remain; no user waveform, per-operator key or velocity scaling, or 4x mode.
- `strata` is an eight-voice wavetable synthesizer in the Magneto look, a redesign inspired by `oxide-dsp`'s Wavetable device
  (`~/github/oxide-studio`, MIT/Apache-2.0), whose single-resolution tables alias at high notes and whose evenly phased unison cancels the fundamental at Amount 0. Strata
  builds each frame in 21 half-octave band-limited mips by FFT and reads the one that stays under Nyquist (a square measures -77 to -89 dB off its harmonics, a
  naive read -12 to -25 dB), starts unison voices in step, antialiases Fold with first-order ADAA, and runs pitch, detune, pan and filter coefficients at a 32-sample control
  rate (the first version cost 51 % of a core for eight notes at the defaults; now 9 %). Two oscillators, sub, unison, one SVF, two ADSRs, an LFO, 41 automatable parameters, no
  plugin state, 300 core checks under ASan/UBSan. See `plugins/strata/docs/design.md`. Listening and host validation in REAPER remain; Bend and Sync still alias at about
  -40 dB, and there is no table import or modulation matrix.
- `infra` is a stereo VCA driven by a beat-locked LFO in the Magneto look (a new plugin, not a port): the LFO runs at a whole multiple of the host beat rate
  (Auto = nearest 10 Hz), clamped to 1-20 Hz, from eight waveforms, with a 0/90/180/270 degree phase per channel and a default 0 to -3 dB range. The LFO is a function of the transport
  position (free-running when stopped), so it is block-size independent. Ordinary parameters, no plugin state, four output parameters for the panel. See `plugins/infra/docs/design.md`.
  Listening and host validation in REAPER remain; the BBT-to-beat mapping is the part most worth checking.
- `trio` is a three-band dynamics processor in the Magneto look, a redesign inspired by `oxide-fx`'s Multiband Dynamics
  (`~/github/oxide-studio`, MIT/Apache-2.0), whose bands share one envelope between the left and right channels and step it
  twice per sample (halving the times and cross-steering the channels), and whose detector reads the rectified wave. Trio has
  adjustable all-pass-compensated LR4 crossovers that sum flat, one peak-hold detector per band linked across the channels,
  a documented gain computer (`bandTargetDb()`), Time and Amount macros, an upward limit, and output parameters for per-band
  gain and level that feed the panel's bars. Ordinary parameters, no plugin state. See `plugins/trio/docs/design.md`. Listening
  and host validation in REAPER remain; no look-ahead, soft knee or external sidechain.
- `octet` is an eight-band parametric equalizer in the Magneto look, a redesign inspired by `oxide-fx`'s EQ Eight
  (`~/github/oxide-studio`, MIT/Apache-2.0), whose 48 dB cuts are four identical Q = 0.707 sections (-12 dB at the cutoff
  rather than -3), whose L/R and M/S modes hard-wire bands 1-4 to one channel, and whose defaults are not transparent.
  Octet has proper 8th-order Butterworth cuts, per-band channel assignment, double-precision filters, a bit-exact
  transparent default, and a panel with draggable band nodes that plots the exact response from the coefficients in use.
  It raises the `MagnetoKit` mirrored-parameter limit from 32 to 64 (octet has 50). Ordinary parameters, no plugin state.
  See `plugins/octet/docs/design.md`. Listening and host validation in REAPER remain; no spectrum analyser.
- `glue` is a stereo-linked bus compressor in the Magneto look, a redesign inspired by `oxide-fx`'s Glue Compressor
  (`~/github/oxide-studio`, MIT/Apache-2.0), whose code is a plain hard-knee peak compressor with no sidechain filter
  and a soft clipper that is discontinuous and on by default. Glue has a soft knee, a sidechain high-pass, peak or RMS
  detection, a linked detector, an auto release driven by the demanded reduction, a continuous soft clip (off by
  default), parallel mix, and output parameters for gain reduction and level that feed the panel's transfer curve, live
  dot and history. Ordinary parameters, no plugin state. See `plugins/glue/docs/design.md`. Listening and host validation
  in REAPER remain; no look-ahead and no external sidechain yet.
- `sam` is a speech instrument in the Magneto look, built around the vendored SAM (Software
  Automatic Mouth) C code from `~/github/SAM`. Keys mode speaks a word per MIDI note, sung at the
  key's pitch (nearest whole SAM pitch tick, then resampled into tune); Transport mode lays the
  words out one per step from a start bar against the host's BBT, with locate and loop handling and a
  **Speak** audition. SAM renders words on the audio thread (30-50 us each); the text is the one
  state. SAM was hardened against the out-of-bounds accesses, divide by zero and infinite loop
  found by fuzzing (`plugins/sam/third_party/sam/PATCHES.md`). **SAM has no licence**: whether
  the bundle may ship in public releases is an open decision. Host validation in REAPER and
  listening tests remain. See `plugins/sam/docs/design.md`.
- `thumper`, `snatch` and `tops` are portable-core drum generators (kick, snare, hi-hat/cymbal) with thin DPF
  wrappers, core tests and `profile.ttl`, with Magneto-look panels (`MagnetoKit`). A Preset is a one-shot applied by
  the UI (it sends its sound values as ordinary parameters), not by the DSP, so a reloaded project keeps its saved
  values. Velocity sets level in all three; each has a Note Map (any note, or General MIDI: kick 35/36, snare 37/38/40, hats
  and cymbals); none keeps plugin state (the host saves the parameters). `clatter` covers the rest of the GM
  percussion map (toms, hand drums, bells, wood, shakers, clap, guiros: 31 sounds in five families, a table-driven
  core with choke groups and per-family trims); whistles and cuicas are not yet covered. All four default to General MIDI.
- `magneto` now exists as an original physically informed combustion engine
  sound generator ported from Baldan et al., SIVE'15: four-stroke cycle
  functions, per-cylinder digital waveguides with valve-modulated feedback and
  piston-modulated length, intake runners, extractors, straight pipe, prime-length
  four-element muffler, tailpipe, overrun-only backfire, manual/CC/host-tempo
  engine speed, listening-position stereo mix, deterministic tests, and a first
  VST3 wrapper target with UI via vendored DPF;
- `chipper` now exists as an original stereo lo-fi processor with bitwise quantization, sample-rate reduction via sample-and-hold, clock jitter, dry/wet mix, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `ghost` now exists as an original audio-triggered ghost-note generator with BBT-quantised 16th-grid ghosts, drum (ch 10) and note modes, four Drift-CC feel controls, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `spliff` now exists as a Spiff-manner adaptive transient processor with a 3-band detector, cut/boost modes, sensitivity, sharpness, decay with LF/HF tilt, mix/trim/delta/bypass, Drift-CC control, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `helterskelter` now exists as an automatic wah pedal with a resonant lowpass driven by the input envelope and/or a BBT-synced ADSR cycle, full ADSR, gate invert, Drift-CC control, deterministic tests, and a first VST3 wrapper target with UI via vendored DPF;
- `treatment` now exists as a physically modelled acoustic treatment panel: a
  mass-air-mass absorber whose resonance follows `w0 = c*sqrt(rho0/(m*D))` and
  whose absorption is maximal at the impedance optimum `r = rho0*c`, so the
  panel has a real sweet spot rather than "more is better". Parameters are real
  panel quantities in mm, kg/m2 and Rayl/m; a seeded Randomise trigger builds a
  new plausible panel while preserving Amount and Bypass; five Drift-CC lanes
  drive the geometry; read-only status parameters report the derived resonance,
  peak absorption and fill diffusion corner; both biquad stages are cuts only
  and the tests assert the cascade never boosts anywhere in the parameter
  space; deterministic tests, and a first VST3 wrapper target with UI via
  vendored DPF;
- `sinevox` now exists as a formant tracker with two voices: an LPC analysis (order
  2N+2 on a ~16 kHz decimated mono sum of inputs 1/2, 5 ms hop) yields up to eight
  persistent tracks of frequency, Q and amplitude, which drive either a parallel
  band-pass bank on inputs 3/4 (vocoder) or a bank of sines (sine-wave speech).
  Shift, Q Scale and Tilt reshape the tracks; MIDI CC and held notes control it;
  the tracks can be emitted as CCs or notes. All settings are parameters, so it has
  no plugin state. Design notes in `plugins/sinevox/docs/design.md`.
- `voxmod` now exists as a combined vocoder and ring modulator sharing one
  four-input bus: a filter-bank vocoder that analyses the carrier on inputs 1/2
  and re-imposes its spectral envelope on the modulator from 3/4, running in
  parallel with a polyBLEP-band-limited ring modulator over the same modulator,
  blended by a single `Mix` parameter rather than a mode switch. Band edges are
  logarithmic so the bank does not starve the formants, and are constant-skirt
  so resynthesis gain does not tilt with band width. `Ring Ratio` is quantised to
  16 musical steps so sideband moves are repeatable, and `Formant` transposes
  the modulator side of the bank only, which relocates the envelope rather than
  transposing the whole signal. `Sync` routes the ring-modulated signal back as
  the vocoder's carrier so inputs 3/4 alone are sufficient. Five Drift-CC lanes;
  read-only status parameters report the effective carrier frequency, carrier
  sibilance and band-gate reduction; deterministic tests, and a first VST3
  wrapper target with a NanoVG UI via vendored DPF;
- `keyframe` now exists as an extrema-sampling time stretch after Nielsen's
  DAFx26-26 method: bandlimited B-spline derivative analysis, difference
  thresholding, subsample extrema location, a three-playhead sparse-domain
  overlap-add engine whose crossfade length follows keyframe spacing, independent
  time and pitch rates, reference hold, fixed reported latency with a matched dry
  delay, deterministic tests, and a first VST3 wrapper target with UI via vendored
  DPF;
- `install.sh` exists as the intended build/install entrypoint for local VST deployment.

Current main gap:

- DPF is now vendored and all current wrapper targets build successfully.
- `install.sh` and `scripts/package-release.sh` now install and package real
  `bassgen.vst3`, `p_mix.vst3`, `e_mix.vst3`, `m_mix.vst3`, `t_mix.vst3`,
  `mixgen.vst3`, `loopdelay.vst3`, `lightverb.vst3`, `melgen.vst3`,
  `rift.vst3`, `orchid.vst3`, `ambo.vst3`, `drumgen.vst3`, `drumkit.vst3`,
  `cadence.vst3`, `arpgen.vst3`, `counterpointer.vst3`, `sidecar.vst3`, `gremlin.vst3`,
  `gremlin_driver.vst3`, `ground.vst3`, `floozy.vst3`, `basilico.vst3`,
  `canticle.vst3`, `moka.vst3`, `luma.vst3`, `paunchlad.vst3`, `lifeform.vst3`,
  `plank.vst3`, `pratt.vst3`, `retune.vst3`, `sprout.vst3`, `markov.vst3`, and
  `xoxolo.vst3`, `syrinx.vst3`, `tuney_vst.vst3`, `harmonic_atlas.vst3`,
  `conductor.vst3`, `drift.vst3`, `mnemosyne.vst3`, `polymeter.vst3`,
  `oracle.vst3`, `mosaic.vst3`, `resonance_garden.vst3`, `orbit.vst3`,
  `guardian.vst3`, `campione.vst3`, `skream.vst3`, `chipper.vst3`, and
  `magneto.vst3`, `ghost.vst3`, `spliff.vst3`, `helterskelter.vst3`,
  `treatment.vst3`, `voxmod.vst3`, `sinevox.vst3`, `keyframe.vst3`, `beards.vst3`, `trajectory.vst3`, `fathom.vst3`, `qune.vst3`, `grist.vst3`, `sam.vst3`, `ladder.vst3`, `glue.vst3`, `octet.vst3`, `trio.vst3`, `infra.vst3`, `strata.vst3`, `quartet.vst3`, `monochord.vst3`, and `nam.vst3` bundles.
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
  twelve-voice pitch/energy view (voice limit 1-12, internal scale as a 23-scale dropdown).
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
