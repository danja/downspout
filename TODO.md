# Downspout TODO

Open items only. Finished work is recorded in the plugin docs and `MISTAKES.md`.

## Needs your approval first (shared or root code)

* **Promote the shared `OnePoleLowpass` / `DCBlocker` / `BiquadFilter` duplicates into
  `include/downspout/dsp/`.** Worth doing but a separate change, and CLAUDE.md requires
  approval before touching shared headers. HANDLE WITH CARE.
* **Replace the 36 copy-pasted `-UNDEBUG` + include-dir blocks with one root helper**, e.g.
  `downspout_add_core_test(<target> SOURCES ... LIBS ...)`, so a new plugin cannot forget
  either. Low urgency, since `include/downspout/test_assert.h` already makes a lost
  `-UNDEBUG` a compile error. Touches root glue and ~36 plugin CMake files, so it needs
  approval and a full test run.

## Session state (found 2026-10-06)

`scripts/check-plugin-state.sh` reports **42 correct / 0 broken / 22 no state** (see the note below: "no state" is not data loss). The six
plugins that silently wrote their defaults into every project (chipper, damiano, ghost,
helterskelter, skream, spliff) now have `WANT_FULL_STATE` and a `getState()`.

- [ ] **Verify those six in a host.** The failure was silent, so a green build proves
      nothing and the core tests never touch the wrapper. Change a setting, save,
      reopen, confirm it stuck.
- [x] **lifeform, luma and paunchlad now have session state** (2026-10-06), following
      Plank's pattern: one versioned, symbol-keyed text state, bad input rejected without
      touching the patch, triggers never stored. Lifeform and luma also save the live
      64-cell pattern as a bitmap (restoring lifeform's `seed` deliberately does not
      reseed over it); paunchlad's pad cells are momentary so only its nine settings
      are stored. Each got round-trip, rejection, trigger and host-activation tests.
      **Verify in a host**, as with the six above.
- [x] **gremlin, gremlin-driver and flues-synth-driver keep the patch** across host
      activation and sample-rate change (2026-10-09); verify in a host when they get state.
- [ ] **Confirm in a host that the 22 "parameters only" plugins survive save/reopen** (arpgen,
      basilico, canticle, conductor, drift, drumkit, floozy, flues-synth-driver, gremlin-driver,
      gremlin, guardian, harmonic-atlas, m-mix, mixgen, moka, oracle, orbit, polymeter,
      resonance-garden, sprout, syrinx). DPF's VST3 `getState` saves every non-output,
      non-trigger parameter by symbol (`third_party/DPF/distrho/src/DistrhoPluginVST3.cpp:1169`),
      so they need no `WANT_STATE`; `scripts/check-plugin-state.sh` now reports them as
      "parameters only" rather than data loss (2026-10-09). Real state is only needed for data
      outside parameters (patterns, sample paths, text), as in lifeform, luma, paunchlad, plank.
- [x] `gater` state macros removed (2026-10-09): it has no parameters and only momentary MIDI state.
- [ ] Re-run the `-Wall -Wextra` sweep after any state change: the wrapper callbacks are
      what the core tests never touch.

## Sam (added 2026-10-22)

- [ ] **Decide whether `sam.vst3` may be released.** SAM has no licence (abandonware, copyright
      SoftVoice, Inc.; `plugins/sam/third_party/sam/SAM-LICENSE.md`). It is vendored, so a public
      tag ships it, and `scripts/package-release.sh` now requires the bundle. To hold it back,
      build with `-DDOWNSPOUT_BUILD_SAM=OFF` and drop `sam.vst3` from the lists
      `scripts/check-bundle-lists.sh` checks.
- [ ] **Validate in REAPER**: both modes, with a real transport (loop region, relocate, tempo
      change) and the saved text surviving save and reopen. So far the DPF wrapper has been driven
      only through the JACK standalone with a throwaway client (MIDI notes, and BBT as timebase
      master): Keys speaks the right words at the right times, and Transport output matched the
      core's offline render at a constant lag (correlation 0.989-1.000, 0.3 ms jitter, which is
      JACK's tick resolution), with the panel highlighting the spoken word. No relocation, loop
      wrap or state save/reload has been run through the wrapper.
- [ ] **Listen**: tuning near the top of the keyboard (formants move with the retune), SAM's
      aliasing against the **Tone** default, and whether Keys mode wants a sustained or looped word.
- [ ] Not built, see `plugins/sam/docs/design.md`: a phrase mode (one note speaks the whole text),
      formant correction after the retune, CC mapping, Speak in Keys mode.
- [x] `scripts/package-built-bundles.sh` (the advisory macOS/Windows list) was missing beards, grist and
      qune; fixed 2026-10-23 when ladder was added, and `scripts/check-bundle-lists.sh` is clean.

## `-Wall -Wextra` sweep: what is left

Standalone `g++ -std=c++20 -Wall -Wextra -fsyntax-only` over `plugins/*/src/*.cpp` (add
`-Iplugins/generative-common/include` or conductor, drift, ghost, guardian, harmonic-atlas
and others will not compile). No `-Wswitch` warnings remain. What was left on purpose:

* `cadence` and `drumgen` `memset` calls are `void*`-cast, so they no longer warn. (Re-swept
  2026-10-10: zero warnings across `plugins/*/src/*.cpp`.) `cadence_clear_progression` now
  resets slots to `ChordSlot{}` (velocity 96) instead of zeroing; cadence tests pass.
* `campione` loader: the `smpl` loop end is inclusive in the WAV spec, but the engine wraps at
  `position >= loopEnd`, so the last loop sample is skipped (one frame). Changing it means
  adjusting load, save and the engine together; decide whether it is worth it. (The ignored
  `parseSmplChunk` result is now explicit: a short chunk is the same as no chunk. Only the first
  loop record is read, which is documented in the code.)

## Qune

- [ ] **Listen to it.** Every number in `plugins/qune/docs/design.md` is from synthetic vowels,
      noise and silence. Run a real vocal, a bass and a lead through all three tiers and
      judge the artefacts the tests cannot: smearing in Good on low notes, a "tube" sound in
      Best, Fast's splices on voice. Then tune the tracker defaults (Voicing 50 %, Stickiness
      15 cents, the 0.95 lobe tolerance, the 2-hop jump confirmation) on that material.
- [ ] **Host validation in REAPER.** Latency reporting while Quality or Range changes under
      automation, MIDI note/bend/pedal input, sample-rate change, project save and reopen (all
      settings are parameters, so no state to verify beyond that). The core tests never touch
      the wrapper.
- [ ] **MIDI out in a host.** Core tests cover the messages; check that REAPER routes the
      output to a synth, that legato note-on-before-note-off behaves on a mono synth, and
      whether the detection-time timing wants an optional delay to match the audio latency.
      Possible extras: velocity from input level, a minimum note length, a pitch-bend output
      for the cents the audio still differs from the note (Amount < 100 %).
- [ ] The pitch trace is untested against a host that never refreshes output parameters
      (it would stay empty, with its "feed a voice" hint); try Bitwig and Ardour as well.
- [ ] Good tier latency is 2.4 periods of the range floor (28 ms for Voice). Ideas to lower
      it: shorter asymmetric grains, or choosing the floor from the first tracked pitch.
- [ ] Optional: a per-note enable mask on top of the 24 scales; a manual "hold this note" key.

## Plank

- [ ] Host validation in REAPER, especially the resonator engines: CPU load, audio out, and
      the patch surviving playback start and a sample-rate change (the fixes are covered by
      offline tests only, and the original "no audio" symptom was never reproduced offline).
- [ ] Plinky's other scales (Hirajoshi, Insen, Iwato, Minyo, major and minor triads, fifths,
      Romanian) are not in Plank. Adding them means appending to `docs/scales.md`. The
      microtonal ones (Harmonics, Hexany, Just) also need a cents-based table, which the
      current integer ladder cannot express.
- [ ] The root `README.md`, `docs/plan.md` and `docs/summary.md` still call Plank an
      "eight-string synthesizer". Shared docs; wording agreed, awaiting approval to change.

## Other

* `skream` **Morph LP-HP** (bipolar, default off) is in and tested. Not yet listened to in a
  host; try it with Drift on Cutoff. Decisions worth a listen: the morph weight follows
  cutoff position linearly (t = 0.5 is a notch at the cutoff), and it only reshapes the
  forward filter, not the Scream feedback high-pass.

* **Damiano Linked/Split stereo** is in and tested (per-channel Mode, Drive, Tone, Folds; CC Drive and
  CC Shape per channel on a shared CC channel; see `plugins/damiano/docs/porting-notes.md`). Not yet
  auditioned in a host: try Split with Soft left and Fuzz right, and two Drift lanes on CC Drive / CC
  Drive R. Verify an old project (pre-split state) loads Linked. Open: "shape" is the stepped waveshaper
  mode; a continuous shape (asymmetry/bias) would be new DSP. Tone and Folds are not CC-controlled.

* `voxmod`: host validation in REAPER still pending.
* `sinevox`: host validation in REAPER pending, in particular (1) that the host routes
  MIDI out from an effect, (2) the 4-in layout (speech 1/2, carrier 3/4) and (3) that
  saved projects restore the patch. Not yet auditioned on real speech: high voices
  (f0 > ~300 Hz) may lock onto harmonics; fricatives give wide unsteady tracks. Ideas:
  pitch-aware analysis window, a fixed per-formant frequency range, an optional noise
  source for unvoiced frames in vocoder mode, panel following CC changes (needs an
  output mirror parameter).
* `keyframe`: the leash diagram is static at unity rates because drift is genuinely ~0 there;
  a splice halo was added so it reads live during crossfades. If it still reads dead at
  defaults, the diagram needs a rethink, not another lamp.

## Pratt (new plugin, from `~/github/pratt-synth`)

Fully wired (root CMake option, install/release scripts, workflow, docs, product page,
screenshot script entry and asset). 4 core suites pass, threading suite clean under TSan.
Remaining:

* **Verify in a host (REAPER):** load, play notes, filter an audio track, save/reopen state.
  Nothing has been listened to. Also confirm the host accepts an instrument with audio inputs.
* Listen to drum levels (normalised per hit, consistent at ~0.15); check voice-stealing clicks.
* GM Program mode now queues a warm-up per Program Change (last 8 voices); host-check that
  switching programs on several channels no longer glitches on first notes. Offline-tested only.
* `scripts/capture-plugin-screenshots.sh` captures the real desktop if `DISPLAY` is set
  (see `MISTAKES.md`). Make it always use Xvfb. Shared script, needs approval.
* Catalogue screenshot shows Synth mode (filter dimmed); consider a Synth + Filter capture.
* **Timbre Index** (separate fixed filter chain, default 1 = off) is in and tested; not yet
  listened to. Open: it is limited to one index <= 8192 (degree <= 13), so very dark voices still
  lean on Rolloff; a second chain or a larger limit would need `kMaxSections` raised and a
  warm-up that touches only the indices in use (see `plugins/pratt/docs/high-base.md`).
  Filter mode keeps A x B capped at 8192.

## Subsequence-inspired work (see `docs/subsequent.md`)

* **melgen Inertia** is in and tested; screenshot recaptured. Not yet listened to in a
  host. The Channel selector showed "2" for the default of 1; fixed (selector values are
  now offset by their base).
* **counterpointer Inertia** is in and tested (scoring term; Counterpoint mode only, not
  Bass Descend). Its Routing-column layout was fixed (selectors now share the height so
  Freeze is no longer covered by the buttons, and the header band ends under the header).
* **Retune plugin** (`plugins/retune/`): wired into the root build, install/release
  scripts, workflow, docs and Pages; core suite passes under ASan/UBSan. Remaining:
  host validation, a catalogue screenshot with a real `.scl` loaded (the standalone
  shows 12-TET), `.kbm` support, saving scale contents with the session (only the path
  is saved), and an audition against a per-channel-bend synth.
* **Per-channel pitch bend in existing instruments** (needed for Retune): Retune sends
  one note per channel (2-16) with its own bend. Each instrument must apply pitch bend
  per MIDI channel at a configurable range (default 2 semitones) instead of globally,
  and must not treat different channels as separate parts. Audit and fix, one at a time,
  with a test that two simultaneous notes on different channels bend independently.
  **Done:** `canticle`, `moka`, `floozy` and `syrinx` (per-channel bend, RPN 0 range, voices
  keyed by channel and note where they were keyed by note, tested). They default to a
  2-semitone range, matching Retune. Not applicable (checked 2026-10-09): `plank` (input is
  Launchpad grid presses, not pitched notes) and `magneto` (a note sets engine RPM, no
  pitched voice). **`basilico` done 2026-10-09** (mono: the sounding note's channel bend
  applies, RPN 0 range, tested). **`gremlin` done 2026-10-09** (single source: the
  last-played channel's bend, RPN 0). **`campione` done 2026-10-09** (per-voice, uses its Bend Range
  control, no RPN; see `plugins/campione/docs/pitch-bend.md`). **`mosaic` done 2026-10-09** (per-channel bend with a Pitch bend range control; only
  audible at Pitch range 0).
  `drumkit` is not applicable (notes select unpitched drum pieces). Not yet auditioned with Retune in a host. `pratt` already bends per
  channel and has a Bend Range control (confirm its default suits Retune's 2 st). Also add a Bend
  Range control where a synth has a fixed range, and note the result in each plugin's
  README.
* **Sprout** (`plugins/sprout/`, L-system generator): built, wired, documented, screenshot
  taken. Remaining: host validation, a rule editor (grammars are built in), and listening
  to which grammars sound good at which Step size.
* **Sprout MIDI input** (held-notes pitch source, Drift CC 1-4, Conductor CC 21-24) is in and
  tested under ASan/UBSan. Not yet auditioned in a host: try harmonic-atlas into Sprout with
  Pitch source = Held notes, and Drift or Conductor into its CC channels. Pitch source has a Latched
  mode (the chord survives note-off; the next fresh press replaces it). Open: MIDI is applied
  at block start, not sample-accurately; no pass-through of incoming MIDI.
* **drumgen** has a **Generation every** (1-8 loops, `ca_every`) control for its automaton; tested, not yet listened to.
* **CA lanes in `drumgen` and `xoxolo`** are in and tested. Not yet auditioned in a host: try Rule 90 and 30
  on Hats in drumgen, and marking the hat and percussion lanes in xoxolo. The automaton arithmetic is now
  shared (`include/downspout/cellular_automaton.hpp`, tested in `tests/cellular_automaton_tests.cpp`) and used
  by `polymeter`, `drumgen` and `xoxolo`. Open: xoxolo's grid shows the programmed pattern, not the
  evolved row now playing.
* **Magneto look and feel:** the kit is now shared, `include/downspout/dpf/MagnetoKit.hpp`, used by
  Retune, Sprout, Markov and Damiano (new hooks: overridable `sendValue()` for plugins that carry
  values as state, `setValue()` and `setDim()`). Older generator plugins still use
  `GenerativePanelUI` (mosaic, polymeter, etc.); restyle if wanted. Retune, Sprout and Markov were
  rebuilt against the shared header but not re-screenshotted; confirm they look unchanged.
* **Gravity** is in `harmonic-atlas`. `cadence` (learned harmony) has not had it; its progression
  model is different, so decide whether a gravity bias on the learned transitions makes sense.
* **Atlas voice-leading** is local to `harmonic-atlas`. Listen to it; the chords sit low (around
  C3-C4) because only the bottom and top of the range are penalised, so consider a centre pull.
* **Markov** (`plugins/markov/`, the Markov melody generator) is in and tested (core suite also under
  ASan/UBSan: scales against the docs, walk statistics, order 2, learning, locate and block-size
  independence, Conductor, state format). Not yet auditioned in a host: try each style, Order 2 after
  learning a line, and Learned mix at 50%. Open: learning assumes one melodic line (chords blur the counts);
  the matrix editor has click-to-cycle, drag-to-paint and right-click-to-zero but no undo; the plugin does
  not pass incoming MIDI through; the editor does not move while a CC drives a control.
* **Conductor awareness** was added to `lifeform`, `luma`, `polymeter` and `xoxolo` (off by default; not yet auditioned
  in a host). `counterpointer` and `cadence` followed (the CC mapping is a tested pure function,
  `applyConductorCc`, in their core-types headers; CC 24 relearns). `arpgen` followed (CC 21 Density -> Octaves,
  CC 22 Energy -> Rate; `applyConductorCc` in `arpgen_core.hpp`, tested; screenshot recaptured; not yet auditioned
  in a host). `m-mix` followed (CC 21 Density -> Open Bias, CC 22 Energy -> Maintain inverted; tested; UI slider added and
  the window made taller because its sixth slider row had been hidden under the toggles). Still without it among the generators: `mnemosyne` (only Novelty and a seed re-roll would fit, and its core takes parameters const, so it needs wrapper-level handling), `sidecar` (its wrapper is tangled up with the server path),
  `tuney-vst`. Worth adding where a density or energy knob exists.
* **Jig port (from Farelo, `#jigdaw`):** port Sprout, Markov and Moka from Downspout to Jig
  format. Lives outside this repo; keep their portable cores free of DPF code so they move cleanly.
* Later candidate: chord-graph walker. A shared voice-leading helper in
  generative-common is only worth it if a second plugin needs it (shared code, needs approval).

## Candidates from `~/github/oxide-studio` (reviewed 2026-10-23)

MIT/Apache-2.0 Rust, so porting is allowed. Its README overstates: `DuckShaper`, `MasterClipper`,
`PhaseScope` and `ClubDeck` do not exist in the code. Read the source and check it against its
reference before porting any device. Downspout already covers distortion, granular, pitch/formant,
reverb/delay, resonators and every MIDI generator, so only gaps are listed. In order:

1. [x] **Ladder filter** (`oxide-dsp/src/filter.rs`): done 2026-10-23 as `plugins/ladder/`, fully wired
       (root CMake, install/release scripts, workflow, docs, product page, profile, screenshot). Probed
       against the analytic response first; 87 core checks pass, VST3 builds clean, bundle lists clean.
       Changes from the source: resonance taper, Bass Comp control. Adds a `logarithmic` option to
       `MagnetoKit` ranges (shared). Still to do: host validation in REAPER, listen (does Bass Comp 50 %
       suit as a default? the defaults sit 3.5 dB low), aliasing at high drive (not measured), optional
       Drift CC 1-4 on Cutoff/Resonance (not wired). CPU measured 2026-10-23 on a busy machine (load ~30, so an
       upper bound): about 5.5 % of one core for stereo at 48 kHz (-O2), the same through a long silent decay, so no denormal penalty; it
       could be cheaper (about 20 divides per sample) if it ever matters.
2. [x] **Glue compressor** (`oxide-fx/src/glue_compressor.rs`): done 2026-10-23 as `plugins/glue/`, fully wired. A
       redesign, not a port: the source has a hard knee, no sidechain filter (despite its README), a soft clipper that is
       discontinuous at |x| = 2/3 and on by default, and a 1024-frame cap. Glue has a soft knee, sidechain HPF, peak/RMS,
       linked detector, auto release, continuous soft clip, mix and meters; 139 core checks pass. Still to do: host
       validation in REAPER, listen (is the auto-release table 40-800 ms / 8 dB depth right? is the 10 ms RMS window?),
       external sidechain input (bassops uses inputs 3-4), look-ahead, Drift CC input.
3. [x] **EQ Eight** (`oxide-fx/src/eq8.rs`): done 2026-10-23 as `plugins/octet/` (an eight-band EQ, named to avoid
       Ableton's product name), fully wired. A redesign: the source's 48 dB cuts are four identical Q = 0.707 sections
       (-12 dB at the cutoff, not -3), its L/R and M/S modes hard-wire bands 1-4 to one channel, and its defaults are not
       transparent. Octet has 8th-order Butterworth cuts, per-band channel assignment, double precision, a bit-exact
       transparent default, and a draggable-node response plot. 380 core checks pass. Raised `MagnetoKit` to 64 mirrored
       values. Still to do: host validation, listen, a spectrum analyser (needs a UI data channel), coefficient smoothing
       for fast sweeps, Drift CC input.
4. [x] **Multiband dynamics** (`multiband_dynamics.rs`): done 2026-10-23 as `plugins/trio/`, fully wired. A redesign: the source
       shares one envelope between the two channels of a band and steps it twice per sample (halved times, cross-steered
       channels), reads the rectified wave, and does not sum flat. Trio has all-pass-compensated adjustable LR4 crossovers, one
       peak-hold detector per band linked across channels, Time/Amount/Upward-limit controls and live per-band gain bars; 303 core
       checks pass. Still to do: host validation, listen (the OTT defaults are strong; detector decay times are my choice), soft
       knee, band solo, external sidechain, Drift CC input.
5. [ ] **Wavetable synth** (`oxide-dsp/src/wavetable/`): IN PROGRESS as `plugins/strata/` (not wired into the root build yet, so
       it cannot break anything). Done 2026-10-23: band-limited half-octave mip tables built by FFT (`strata_tables.*`), the
       8-voice engine (`strata_core.*`: two table oscillators, Bend/Sync/Fold warp with ADAA fold, sine sub, unison, SVF filter,
       two ADSRs, LFO, MIDI, DC blocker), `strata_params.hpp` (41 parameters), and 300 core checks passing
       (`tests/strata_core_tests.cpp`, built by hand with g++; no CMake yet). Measured: unwarped inharmonic power -77 to -88 dB at
       every pitch against -12 to -25 dB for the same table read naively; warp aliasing at note 60, amount 0.7: Bend -42.6,
       Sync -38.5, Fold -33.1 dB. The Oxide source has unaliased naive tables and cancelling evenly-phased unison.
       Still to do: `plugins/strata/CMakeLists.txt`, DPF wrapper + UI (copy the `trio` pattern; show the current frame's
       waveform), `profile.ttl`, wire with `python3 <scratchpad>/wire.py` equivalent (root CMake option, install.sh,
       package scripts, workflow, screenshot script, docs lists), docs/product page, screenshot, design.md, CPU timing.
6. [ ] **4-op FM** (`oxide-dsp/src/operator/`): 11 routing algorithms. No FM synth yet.
7. [ ] Lower priority: Electric piano and Collision/Tension physical models (near moka, syrinx,
       magneto), Meld-style engines (Swarm, Formant, Chord, Shepard), Drift-style analog synth.
8. [ ] Skip or only if wanted: spectral resonator and Auto Shift (overlap resonance-garden, qune,
       quefrency), warp engine (overlaps keyframe, qune, grist), stem splitter (crude), groove pool.

## Evaluate Manually in Reaper

* helterskelter
* keyframe
* ladder
* glue
* octet
* trio
* ghost
* spliff
* ambo
* arpgen
* conductor
* drift
* harmonic-atlas
* mnemosyne
* magneto
* moka
* mosaic
* oracle
* orbit
* polymeter
* resonance-garden
* plank
* tuney-vst
* worms
* **Beards host validation and follow-ups.** Load `beards.vst3` in REAPER, route Drift (CC 1-4)
  and Conductor (ch 16) into it, and listen: the DSP is a port but has only been checked
  by core tests and a headless UI capture. Not ported from Beads: delay (multitap) mode,
  CV/gate inputs, wow and flutter, 24 kHz Tape rate. The reverb is a new Griesinger tank,
  not Beads' `fx_engine`. See `plugins/beards/docs/design.md`.

## Recurring - check periodically

* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
