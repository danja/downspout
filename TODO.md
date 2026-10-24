# Downspout TODO

Open items only. Finished work is recorded in the plugin docs and `MISTAKES.md`.

## Shared code follow-ups (approved, partly done)

Done in October: `cmake/DownspoutTests.cmake` (`downspout_add_core_test`) replaced the copy-pasted test blocks in the plugin
CMake files; `include/downspout/dsp/svf.hpp` (Strata, Quartet) and `dc_blocker.hpp` (Drumkit, Syrinx) are shared, both
checked bit-for-bit against the old code. The remaining "duplicates" are not duplicates, so unifying them would change how
plugins sound: the `OnePoleLowpass` copies in Moka, Canticle and Magneto differ in cutoff clamp (40 vs 30 Hz), coefficient
caching and denormal handling; Gremlin's DC blocker has a fixed pole of 0.9985; Drumkit's and Syrinx's `BiquadFilter`
differ in the Q floor (0.5 vs 0.3) and the band-pass gain (`Q alpha` vs `alpha`). Only unify these deliberately, per plugin,
with an A/B listen. The core test blocks in `src/common/sampleprofile` and `tools/ai-coordinator` still use the long form.

## Syrinx follow-ups (October 2026 chirp and audio-input work)

- [ ] Ear pass over all ten presets with Chirp 60 %: Warbler and Pigeon are slow by design (sweeps of 6 and 3 oct/s);
      decide whether they want a higher default Chirp or a per-preset syllable-length scale. Retune Nightjar, Pigeon
      and Starling (air-sac ODE and tracheal coupling presets, already flagged in the design notes).
- [ ] Host check of audio mode as an insert on a voice track (REAPER, Ardour): routing, that the VST3 category
      "Instrument|Synth" still lets the host feed audio in, and the ~64 ms tracker latency (not reported to the host).
- [ ] Compare against Lyrebird when it is on the machine: spectral flatness target (median 0.044 in its harvest, ours
      0.004-0.03 with Chirp on, definitions may differ), syllable length and sweep statistics.
- [ ] Projects saved before the chirp work open with 3 syllables per note (new defaults); consider whether Syllables 1 /
      Chirp 0 should be the default for the `Custom` preset's host recall.

## Session state

`scripts/check-plugin-state.sh`: 0 broken. "Parameters only" is not data loss: DPF's VST3
`getState` saves every non-output, non-trigger parameter by symbol
(`third_party/DPF/distrho/src/DistrhoPluginVST3.cpp:1169`), so those plugins need no `WANT_STATE`.
Real state is only needed for data outside parameters (patterns, sample paths, text), as in
lifeform, luma, paunchlad, plank.

- [ ] **Verify in a host** that these save and reopen: chipper, damiano, ghost, helterskelter, skream,
      spliff (they once silently wrote defaults into every project; they now have `WANT_FULL_STATE`
      and a `getState()`), lifeform, luma, paunchlad (versioned text state, with the live 64-cell
      bitmap for lifeform and luma), and every "parameters only" plugin (arpgen, basilico, canticle,
      conductor, drift, drumkit, floozy, flues-synth-driver, gremlin, gremlin-driver, guardian,
      harmonic-atlas, m-mix, mixgen, moka, oracle, orbit, polymeter, resonance-garden, sprout, syrinx,
      and the seven new ones below). The failures were silent, so a green build proves nothing and the
      core tests never touch the wrapper: change a setting, save, reopen, confirm it stuck.
- [ ] gremlin, gremlin-driver and flues-synth-driver keep the patch across host activation and
      sample-rate change; verify in a host when they get state.

## Sam

- [ ] **Decide whether `sam.vst3` may be released.** SAM has no licence (abandonware, copyright
      SoftVoice, Inc.; `plugins/sam/third_party/sam/SAM-LICENSE.md`). It is vendored, so a public
      tag ships it, and `scripts/package-release.sh` requires the bundle. To hold it back,
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

## `-Wall -Wextra` sweep

Standalone `g++ -std=c++20 -Wall -Wextra -fsyntax-only` over `plugins/*/src/*.cpp` (add
`-Iplugins/generative-common/include` or conductor, drift, ghost, guardian, harmonic-atlas
and others will not compile). Re-run after any wrapper or state change: the wrapper callbacks
are what the core tests never touch.

* Last full sweep 2026-10-23: zero warnings in 159 core files (`plugins/sam` needs `-Iplugins/sam/third_party/sam`).
* `campione` loader: the `smpl` loop end is inclusive in the WAV spec, but the engine wraps at
  `position >= loopEnd`, so the last loop sample is skipped (one frame). Changing it means
  adjusting load, save and the engine together; decide whether it is worth it.

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

## Other

* `skream` **Morph LP-HP** (bipolar, default off): not yet listened to in a host; try it with Drift
  on Cutoff. Decisions worth a listen: the morph weight follows cutoff position linearly (t = 0.5
  is a notch at the cutoff), and it only reshapes the forward filter, not the Scream feedback
  high-pass.
* **Damiano Linked/Split stereo** (`plugins/damiano/docs/porting-notes.md`): not yet auditioned in a
  host: try Split with Soft left and Fuzz right, and two Drift lanes on CC Drive / CC Drive R.
  Verify an old project (pre-split state) loads Linked. Open: "shape" is the stepped waveshaper
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
* **Beards** host validation and follow-ups: load `beards.vst3` in REAPER, route Drift (CC 1-4)
  and Conductor (ch 16) into it, and listen: the DSP is a port but has only been checked
  by core tests and a headless UI capture. Not ported from Beads: delay (multitap) mode,
  CV/gate inputs, wow and flutter, 24 kHz Tape rate. The reverb is a new Griesinger tank,
  not Beads' `fx_engine`. See `plugins/beards/docs/design.md`.

## Pratt

* **Verify in a host (REAPER):** load, play notes, filter an audio track, save/reopen state.
  Nothing has been listened to. Also confirm the host accepts an instrument with audio inputs.
* Listen to drum levels (normalised per hit, consistent at ~0.15); check voice-stealing clicks.
* GM Program mode queues a warm-up per Program Change (last 8 voices); host-check that
  switching programs on several channels no longer glitches on first notes. Offline-tested only.
* Catalogue screenshot shows Synth mode (filter dimmed); consider a Synth + Filter capture.
* **Timbre Index** (separate fixed filter chain, default 1 = off) is tested; not yet listened to.
  Open: it is limited to one index <= 8192 (degree <= 13), so very dark voices still lean on
  Rolloff; a second chain or a larger limit would need `kMaxSections` raised and a warm-up that
  touches only the indices in use (see `plugins/pratt/docs/high-base.md`). Filter mode keeps
  A x B capped at 8192.

## Generators and MIDI tools (host and listening checks, small follow-ups)

* **melgen Inertia** and **counterpointer Inertia** (scoring term; Counterpoint mode only, not Bass
  Descend): not yet listened to in a host.
* **Retune** (`plugins/retune/`): host validation, a catalogue screenshot with a real `.scl` loaded (the
  standalone shows 12-TET), `.kbm` support, saving scale contents with the session (only the path is
  saved), and an audition against a per-channel-bend synth.
* **Per-channel pitch bend** (needed for Retune): done for canticle, moka, floozy, syrinx, basilico,
  gremlin, campione, mosaic, strata, quartet, monochord (pratt already bends per channel; plank, magneto and drumkit are not
  applicable). Remaining: audition them with Retune in a host, confirm pratt's default Bend Range suits
  Retune's 2 st, and add a Bend Range control where a synth has a fixed range, noting it in each README.
* **Sprout**: host validation, a rule editor (grammars are built in), listening to which grammars sound
  good at which Step size; its MIDI input (held-notes pitch source, Drift CC 1-4, Conductor CC 21-24) is
  not yet auditioned (try harmonic-atlas into it, Pitch source = Held). Open: MIDI is applied at block
  start, not sample-accurately; no pass-through of incoming MIDI.
* **drumgen** (`Generation every`) and **CA lanes in drumgen and xoxolo**: not yet listened to (try Rule 90
  and 30 on Hats; mark the hat and percussion lanes in xoxolo). Open: xoxolo's grid shows the programmed
  pattern, not the evolved row now playing.
* **Magneto look and feel:** `include/downspout/dpf/MagnetoKit.hpp` is shared by Retune, Sprout, Markov,
  Damiano and the newer instruments and effects. Older generator plugins still use `GenerativePanelUI`
  (mosaic, polymeter, etc.); restyle if wanted. Retune, Sprout and Markov were rebuilt against the shared
  header but not re-screenshotted; confirm they look unchanged.
* **Gravity** is in `harmonic-atlas`. `cadence` (learned harmony) has not had it; its progression model is
  different, so decide whether a gravity bias on the learned transitions makes sense.
* **Atlas voice-leading** is local to `harmonic-atlas`. Listen to it; the chords sit low (around C3-C4)
  because only the bottom and top of the range are penalised, so consider a centre pull.
* **Markov**: not yet auditioned in a host (each style, Order 2 after learning a line, Learned mix at 50 %).
  Open: learning assumes one melodic line (chords blur the counts); the matrix editor has click-to-cycle,
  drag-to-paint and right-click-to-zero but no undo; no MIDI pass-through; the editor does not move while a
  CC drives a control.
* **Conductor awareness** (lifeform, luma, polymeter, xoxolo, counterpointer, cadence, arpgen, m-mix; off by
  default) is not yet auditioned in a host. Still without it: `mnemosyne` (only Novelty and a seed re-roll
  would fit, and its core takes parameters const, so it needs wrapper-level handling), `sidecar` (its wrapper
  is tangled up with the server path), `tuney-vst`.
* **Jig port (from Farelo, `#jigdaw`):** port Sprout, Markov and Moka from Downspout to Jig format. Lives
  outside this repo; keep their portable cores free of DPF code so they move cleanly.
* Later candidate: chord-graph walker. A shared voice-leading helper in generative-common is only worth it
  if a second plugin needs it (shared code, needs approval).

## New plugins from the oxide-studio review (October 2026)

All seven are built, wired and tested (core suites under ASan/UBSan where noted in their `docs/design.md`).
None has been listened to or run in a host. Remaining per plugin:

* **ladder**: listen (is Bass Comp 50 % the right default? the defaults sit 3.5 dB below unity); aliasing at
  high drive is not measured.
* **glue**: listen (is the auto-release table 40-800 ms / 8 dB depth right? the 10 ms RMS window?);
  check in a host that Look-ahead's reported latency is compensated.
* **octet**: listen to and look at the new spectrum analyser in a host (Off/Pre/Post; does the 40 dB/s fall and the 0 dB-at-full-scale scale read well? is a 4.5 dB/oct tilt wanted?); the CC band mapping (Frequency/Gain/Q of one band + Output) is my choice: confirm it suits a Drift lane.
* **trio**: listen (the OTT defaults are strong; the detector decay times are my choice); check the sidechain
  routing in a host.
* **strata**: listen; oversampled warps (Bend -42.6 dB, Sync -38.5 dB, Fold -33.1 dB aliasing at note 60,
  amount 0.7); table import; a modulation matrix; Fold + Sync
  worst case is about 90 % of a core.
* **quartet**: listen; user waveform (the source's 16-harmonic one); per-operator key and velocity scaling;
  4x oversampling for bright FM at the top of the keyboard.
* **monochord**: listen; sympathetic strings / several strings per note; frequency-dependent T60; bow noise.
* Decided not to build (revisit if wanted): **Electric piano** and **Collision** (a few modal filters each,
  covered by Moka and Quartet); the **Meld** engines (Swarm, Harmonic, Sub, NoiseLoop, FM, Fold, Chip,
  Bitcrush, Formant, Chord, Resonator, Shepard: mostly overlap Strata and Quartet; Shepard and Chord are the
  least covered); a **Drift-style analog synth** (overlaps Basilico and Strata with a filter); spectral
  resonator and Auto Shift (overlap resonance-garden, qune, quefrency), the warp engine (overlaps keyframe,
  qune, grist), the stem splitter (crude), the groove pool.

## Evaluate Manually in Reaper

helterskelter, keyframe, ladder, glue, octet, trio, strata, quartet, monochord, ghost, spliff, ambo,
arpgen, conductor, drift, harmonic-atlas, mnemosyne, magneto, moka, mosaic, oracle, orbit, polymeter,
resonance-garden, plank, tuney-vst, worms, beards.

## Recurring - check periodically

* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
* read INBOX.md and fold tasks in here; check `dim` for Farelo tasks relevant to Downspout (read-only)
