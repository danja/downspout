# Downspout TODO

Open items only. Finished work is recorded in the plugin docs and `MISTAKES.md`.

## Priority

Sam plugin isn't accepting text input, the edit box is broken. In the JACK standalone it works: with the
  standalone's own port (`SAM_PORT=<port> scripts/sam-say.sh`; 7230 is the REAPER-hosted Sam) Transport,
  Clear and typing all reach the engine. Under Carla as a VST3 the buttons and Clear land but typed
  characters arrive partially or not at all, so the fault lies in the VST3 key/character path, not the
  edit box core. Next: trace `third_party/DPF/distrho/src/DistrhoUIVST3.cpp` `onKeyDown` ->
  `DistrhoUIInternal.hpp` `handlePluginKeyboardVST` -> `onCharacterInput`, and check whether space and
  focus changes swallow keys.

Sieve is not usable yet: the wet path is broken. With a mask of unity in every bin, an STFT round
  trip must be a pure delay; instead white noise comes out 17.7 dB down and uncorrelated with the
  input (best-lag correlation 0.0085 over 2 048 lags), and a sine sitting on a scale note is
  cancelled to -43 dB rather than passed. In `processBlock` the new frame is written at
  `base = frameCounter % kFftSize` and read at `outIdx = (frameCounter - kFftSize) % kFftSize`,
  which is the same index: each output sample is read the instant it is written (at `window[0]` = 0)
  while the three other overlap-add contributions arrive 1 024 samples later. Dead code sits just
  above at `src/sieve_engine.cpp:192-196` ("align? use simple"). Fix the FIFO/overlap-add
  alignment, then replace the 2-assert smoke test with a real one (unity mask = delayed input;
  on-scale sine passes within a dB; a sine between the notes is attenuated by the stated **Floor**)
  before touching the panel. Identity was wrong too and is fixed 2026-11-08: the wrapper said
  "Mouth", used Mouth's URI, CLAP id and `Mout` unique id, so a DAW listed it as a second Mouth.

Sieve and zmorph "core tests" are smoke tests (2 and 1 `assert`, both via `<cassert>` without
  `downspout/test_assert.h`, so they would pass vacuously under `NDEBUG`). Scope has 20 checks and
  the header. Bring all three up to the usual standard: assert observable effects, and show every
  control has an audible or visible effect.

Chorale should allow syncing to transport BBT. Confirmed still open: `plugins/chorale/docs/design.md`
  lists a tempo-synced rate under "Not built", and the plugin reads no host tempo or BBT.

Cleared 2026-11-08: NAM is built and wired (`-DDOWNSPOUT_BUILD_NAM=ON`, 99/99 `.vst3` bundles in
  `build/bin`, all release/install/screenshot/Pages lists). Sieve/zmorph/scope now have full Pages
  front matter, a group (so they appear in the catalogue), `sieve.png` and `zmorph.png` captured;
  `scripts/package-built-bundles.sh` lists all three; the empty `plugins/perc/` skeleton is gone.

## Fun

Make a new plugin that will be entirely your design, except for two constraints : it shouldn't overlap too much with existing Downspout plugins and it should be *interesting*. Document the key steps of the development process in a file under docs/ - this should be written in a way that can help other agents build plugins.

Add two Ascending patterns to Bassgen corresponding to the existing Descending ones.

## New plugin proposals (from the DSP-Ports-JSFX survey, 2026-11-03)

Source: `~/github/DSP-Ports-JSFX/Plugin Ports/*.jsfx`, used as **reference**. Downspout is MIT. Where the JSFX
original is MIT (JuKnow Chorus, ChronoDupe, Airwindows ports, lkjb ports) code may be copied with the notice kept.
Where it is GPL (x42, zam, setBfree, Aeolus, amsynth, Dexed, cStop, chmaha's own designs) write a clean-room
implementation from the published algorithm and do not translate the JSFX. `scope` now supplies metering and analysis;
before it the repository had none.

- [x] **Tape Stop** built as `spindown` (clean-room; cStop is GPLv3): core, DPF wrapper, UI, 85 core checks, lists and docs.
      Still open: listen to it; validate in REAPER (MIDI trigger routing to an effect, Cycle against a real BBT
      transport with loop and relocate, save/reopen, Drift CC 1-4); decide whether it wants a tone darkening as the
      tape slows, tempo-synced Stop/Start times, and a "resume" option instead of the crossfade to live. See
      `plugins/spindown/docs/design.md` ("Not built").
- [x] **Chorus** built as `chorale` (JuKnow Chorus / junologue-chorus, MIT, notice kept): Juno modes I, I+II, II plus a Custom 1-4 voice mode
      (Rate, Depth, Delay), Tone, Width, Mix, CC 1-4. 205 core checks incl. a reference transcription of the source, mutation-checked.
      Open: listen (Custom voicing by ear), REAPER validation, possibly feedback/flanger, tempo-synced rate, a vibrato (100 % wet) switch.
- [x] **Meters / scope** built as `scope`: spectrum analyzer + level meter (peak, RMS, correlation). Audio passes through.
      64 log-spaced bands (20 Hz - 20 kHz), labelled frequency/dB grids, Range 60/90/120 dB, Peak hold, Spectrum/Levels
      display; audio passes through. Core tests + VST3 build pass, and the live display was verified in the JACK standalone
      with a 1 kHz sine (recorded pass-through measured -6.02 dBFS peak, -9.03 dBFS RMS). Open: try it in REAPER and while
      listening; EBU R128 loudness, goniometer and K-Meter are later.
- [x] **Rotary speaker** built as `whirligig` (clean-room; Dizzy/b_whirl is GPL): LR4 crossover, drum and horn rotors with inertia, two mics
      (lobes + Doppler), CC 1/2, and Beat sync (beats per turn, phase-locked to host BBT, Sync Phase). 104 core checks, mutation-checked.
      Open: listen and voice by ear; validate in REAPER (BBT lock across loop/relocate, CC 1/2); possible cabinet/overdrive stage; ~0.7 ms
      dry/wet latency is not reported to the host; input is summed to mono.
- [x] **De-esser** built as `silken`, a vocal strip (original design): rumble filter, plosive guard, gate, harshness tamer, de-esser (Band/Wide, Listen, Range), leveller
      (compressor), mud / warmth / presence / air, drive, sparkle, output. 301 core checks, mutation-checked. Open: try on real vocals and voice by ear (all times, ratios and
      corners are by ear), REAPER validation. Parameter ids are append-only: 0-17 shipped first, 18-31 added later.
- [ ] **Vocal extras left out of `silken`** (to keep its panel readable; each could be a plugin or a later addition): breath reducer (a gate/expander keyed on unvoiced
      low-level HF between words); dynamic resonance suppressor (multi-band version of the harshness tamer); vocal doubler / ADT and stereo widener (Chorale is close
      for the chorus part); a formant-aware sibilance detector; look-ahead for the de-esser and plosive guard (needs latency reporting); oversampled drive and sparkle;
      mid/side de-essing; a dedicated vocal limiter / clipper at the end of the chain; auto-gain / vocal rider (slow level riding before the compressor).
- [ ] **Gate** (ZamGateX2 reference, GPL, clean-room): standalone audio noise gate with sidechain.
- [ ] **MIDI filter / router** (Spaghetti Junction, x42 midifilter, GPL): channel/note filter and remap.
- [x] **Tonewheel organ** built as `wheelhouse` (clean-room; setBfree is GPL): core, DPF wrapper, UI with custom drawbars,
      104 core checks, lists and docs. Still open: listen to it and tune the by-ear voicing (percussion times and levels,
      scanner depths, click level, master gain, drive law, the drop in drawbar level when percussion is on); validate in
      REAPER (three MIDI channels from one keyboard track, save/reopen, drawbar drag and scroll in the real UI); CPU of a
      full chord on a slow machine. Not built, see `plugins/wheelhouse/docs/design.md`: Leslie (the rotary speaker proposal
      above is its natural partner), reverb, hardware drawbar CCs, tonewheel leakage and tuning error, key transpose or split.
- [ ] Lower priority: ADT/doubler (ChronoDupe, MIT), Inflator, spectral compressor, dynamic tilt, channel strip,
      pipe organ (Aeolus), subtractive polysynth (amsynth), GM/SoundFont synth.

## New plugins (from INBOX, 2026-11-06)

- [ ] **PadGen** (new plugin, from INBOX): design and build a pad generator. Decide scope (ambient pad
      synth vs MIDI generator vs both), sound engine, and how it differs from existing instruments
      (Basilico, Canticle, Moka, Quartet, Strata, Monochord). Follow the new-plugin checklist.
- [ ] **Audio-to-MIDI generator** (new plugin, from INBOX): generate polyphonic MIDI output from audio input, with
      various processing options and a user-selectable input-signal type (melody / bass / chords / drums).
      Overlaps Qune (pitch tracking) but broader; reuse Qune's tracker if it fits (shared code, needs
      approval). Follow the new-plugin checklist.
- [ ] **Survey `https://github.com/Ka1-S/LYNX`** (Farelo task t20259e38295b) for potential plugins;
      report candidates and whether any overlap existing Downspout plugins.

## Spectrogram painter (built 2026-11-03 as `fresco`; heard by nobody yet)

Built: `plugins/fresco` (2048-point STFT, magnitudes replaced by the picture column under a playhead; Input-phase or Tone source; free or host-timeline playhead; log or
linear axis; Range, Contrast, Pixels, Follow input level, Mix, Output; picture path saved as DPF file state; latency 2048 reported). PNG (own inflate), PGM/PPM and BMP
decoders written in the plugin, so nothing was vendored. Core tests 23k checks, mutation-checked, damaged-file fuzz under ASan/UBSan.

Left out, in rough order of value:
- [ ] **Listen and look**: run it in REAPER behind a spectrogram (white noise and a pad in; Tone and Input sources); check the loudness of default pictures, whether Range 60 dB and
      the 0.02 bin amplitude are sensible, and how the default Follow input level 100 % feels (silence in is silent out: is that the right default?).
- [ ] **JPEG** (baseline and progressive): needs a decoder written in the plugin (a few hundred lines: Huffman, IDCT, upsampling) or a vendored single header, which needs approval.
      Most pictures people have are JPEG, so this is the biggest gap; until then the plugin tells the user to save as PNG.
- [ ] **Video**: step through frames (one picture per column of time). Needs a decoder dependency (FFmpeg or similar): ask before adding. A raw frame-sequence folder would avoid it.
- [ ] Interlaced PNG (refused with a message today), GIF, WebP.
- [ ] An independent end-to-end test: render noise through a picture, compute a spectrogram with a separate STFT, and report the correlation with the picture (the current tests check
      single bands, level, axis, time and polarity against formulas).
- [ ] Fine pictures alias on the lower part of a log axis (a bin takes the interpolation of the rows it covers, not their average): average over the bin's row span.
- [ ] A time-stretch separate from Length, a sync of Length to bars (Whirligig style), a Floor/dither control so very dark pixels do not vanish, a live column plot in the panel.
- [ ] Transient-preserving or phase-locked Input mode (the current Input mode can smear drums).

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

## NAM follow-ups (October 2026, first version)

- [ ] Audition in REAPER with real captures and cabinet IRs: gain staging at the defaults (the -18 dB normalise target versus
      each capture's own input level), the 120/750/3500 Hz tone points, the gate on a noisy guitar. Check the ~37-sample reported latency at 44.1 kHz.
- [ ] CPU: a standard WaveNet is ~37 % of a core per network (Stereo doubles it). Consider an Eco switch that runs one network and copies
      it, a slimmable-model size control, or an A2 recommendation in the panel text.
- [ ] Input-level calibration from the model's `input_level_dbu` metadata (the NAM plugin's "calibrate input"), and a model-change crossfade.
- [ ] macOS and Windows: the vendored NAM/Eigen build is untried there (`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` needs CMake 3.24).
- [ ] Wrapper-level test in ctest: `plugins/nam/tests/wrapper_probe.cpp` is manual because it needs the DPF objects; wire it up if a second plugin wants the same.

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

* Last full sweep 2026-11-06: zero warnings in all plugin core files under `-Wall -Wextra` (nam
  shows only unused-parameter warnings in vendored NAM headers). Includes the new shared
  `voice_leading.hpp` and the markov/bassgen/cadence/drumgen changes.
* `campione` loader: the `smpl` loop end is inclusive in the WAV spec, but the engine wraps at
  `position >= loopEnd`, so the last loop sample is skipped (one frame). Changing it means
  adjusting load, save and the engine together; decide whether it is worth it.

## Qune

- [ ] **Bug (from INBOX): Qune does not stick to notes enough** — the output defaults to being
      almost identical to the input. Investigate the quantisation/hold logic so the emitted MIDI
      follows the detected pitch more aggressively instead of passing the input through. Check the
      default Amount / stickiness and the note-confirmation hops before blaming the tracker.
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

## Treatment

- [ ] **Host validation in REAPER** (Farelo task t545a1867c482, "multiple panels / special effect"):
      the multi-panel bank, Ring mode and geometry sweep are built; validate in a host (save/reopen,
      synced sweep against a real transport, CC 1-5). Not yet auditioned by ear.
- [ ] Bank/sweep audition: is the Ring cap (+9 dB) and the default panel 2-4 geometries right?

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
* [x] **Review `~/github/auricle-algo`** (from INBOX): findings recorded in
      `docs/auricle-algo-review.md`.
* [x] **Shared voice-leading helper** (from auricle review): created
      `plugins/generative-common/include/voice_leading.hpp` (header-only: resolvePitch,
      resolveChord), compiles and integrated into markov (octave placement), bassgen
      (noteFromSemitoneOffset) and cadence (nearest_midi_for_pc). melgen and
      counterpointer use scale-note search rather than fixed-pitch-class octave
      selection, so no direct adoption there.
* [x] **Markov stay/leap params** (from auricle review): added `stay_prob` and `leap_prob`
      params (default off), wired into walkNote, UI, profile.ttl; tests pass.
* [x] **Drumgen voice links** (from auricle review): added `voiceLinksEnabled` control plus a
      deterministic post-pass (snare->clap answer, kick->clave gap fill, closed_hat<->open_hat
      interlock), serialized (voiceLinks=1), gated; tests pass. Full editable link graph is
      not implemented.

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
treatment,
arpgen, conductor, drift, harmonic-atlas, mnemosyne, magneto, moka, mosaic, oracle, orbit, polymeter,
resonance-garden, plank, tuney-vst, worms, beards.

## Recurring - check periodically

* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
* read INBOX.md and fold tasks in here; check `dim` for Farelo tasks relevant to Downspout (read-only)

## Prism

- [ ] **Listen to it.** Core tests only; nothing has been auditioned or run in a host.
- [ ] LFO modulation of delay/feedback (the Multiway plugin has it). Tempo sync is done; it reads only the host tempo, not the bar position.

## Filter-plugin ideas (November 2026 brainstorm)

Mouth (vowel/formant morph filter) is built (core, VST3, tests, screenshot, docs); the rest are not started. Design for Mouth: `plugins/mouth/docs/design.md`.

- [x] **Scale sieve** built as `sieve`: STFT mask (sqrt-Hann, 1024-pt, hop 256), keeps partials near a scale's notes (or held MIDI notes) in every octave. Controls: Root, Scale, Source, MIDI In, Width, Floor, Tilt, Mix, Level. VST3 build passes; the wet path does not work yet and the core test is a smoke test — see Priority.
- [ ] **Audio-driven string / comb (extended Karplus-Strong)**: feedback comb with damping low-pass, pick-position comb and stiffness all-pass chain (clean string -> inharmonic bell -> metal), tuned from MIDI or tracked pitch. Closest existing plugins: `resonance-garden`, `spense`; build after the other two. References: CCRMA "Extended Karplus-Strong" (https://ccrma.stanford.edu/~jos/pasp/Extended_Karplus_Strong_Algorithm.html).
- [x] **Z-plane morph filter** built as `zmorph`: morphs between low-pass and high-pass shapes at chosen cutoff/resonance. Controls: Cutoff, Resonance, Morph, Mix, Level. VST3 build passes, the response is measured (see the product page); the core test is a smoke test and nothing has been auditioned.
- [ ] Envelope-follower multimode filter (auto-wah). Easy, but a crowded category.
- [ ] **Mouth: listen to it** and try it in a host (nothing auditioned). Check Voice switching for clicks on a loud sustained source and that the vowel data matches the Csound manual table (typed from memory of it).
- [ ] Mouth phase 2: tempo-synced vowel sequencer (step list of vowel corners), reusing Prism's tempo plumbing.

## Noise-reduction plugin (idea, November 2026; not started)

Based on MOTU's Arboretum Ionizer (https://motu.com/products/motuaudio/plug/arboretum/ion.html): a multiband
spectral processor with 512 gated bands per channel, linear-phase, used for noise reduction, EQ, dynamics,
spectral morphing and keying. Aim for the noise-reduction side first; it is steady-state broadband noise
(hiss, hum, HVAC, camera noise) removed in real time without the chirps, flanging and ringing that cheap
spectral gates leave behind. Build from the general technique (STFT, per-band noise estimate, gain
smoothing), not from the product.

- [ ] **Core**: STFT analysis/resynthesis (2048-4096 FFT, 75 % overlap) with a per-bin noise-floor estimate and a per-bin gain (spectral gate / subtraction). Reuse the Prism/Qune STFT engine if it fits (shared code, needs approval before touching); otherwise keep it plugin-local.
- [ ] **Noise profile**: a *Learn* mode that averages the spectrum of a noise-only passage into a fingerprint, plus an automatic mode that tracks the floor (minimum statistics) with no learning pass. The profile is state saved with the project (not a parameter; see `scripts/check-plugin-state.sh`) and visible in the panel.
- [ ] **Controls**: reduction amount (dB), threshold offset, attack/release or time smoothing, frequency smoothing, and a band-limit so hum (50/60 Hz and harmonics) can be handled separately from hiss.
- [ ] **Musical-noise suppression** is the hard part and the selling point: smooth the gain over time and frequency, and decide per bin with a decision-directed estimate; the goal is no tonal "chirps" when the signal is only noise. Write the probe before the DSP (CLAUDE.md "measure before building"): white noise in -> residual spectrum flatness and a tonal-artifact measure out, and a speech-like signal with noise -> noise reduced by the stated dB without damaging the signal beyond a bound.
- [ ] **Latency**: report the FFT latency to the host; test that it is compensated, and that block-size changes do not move it.
- [ ] **Stereo**: link the two channels' gains (default) so the image does not wander; optionally process mid/side.
- [ ] **UI**: Magneto look and feel; spectrum display with the noise fingerprint, the threshold curve and the gain reduction; a *Listen to removed noise* (difference) switch to hear what is being taken out.
- [ ] Tests: deterministic core tests (`downspout_add_core_test`), no allocation on the audio path, denormal-safe, sample rates 44.1-192 kHz, `activate()` keeps the profile.
- [ ] Later, from the same product: spectral keying (one input's spectrum gates or ducks another, via sidechain) and frequency morph (impose one sound's spectral curve on another). Both overlap `prism`, `quefrency` and `bassops`; decide after the noise reduction exists.
- [ ] Docs per the new-plugin checklist (install/release lists, Pages product page and group in `_data/groups.yml` (probably `dynamics` or `spectral`), plugins-summary, screenshot).

## Bassops input delay (added November 2026)

- [ ] **Listen and try in a host.** The two delay sliders are tested in the core (exact shift, tempo lock, crossfade, a bass note held behind a kick) and the panel and status read-out were driven through the standalone, but nothing has been auditioned or run in a DAW. Check: the beat slider follows REAPER's tempo changes; the delay feels right against a real kick; 10 ms crossfade is inaudible while dragging; automation of `delay_beat` / `delay_ms`; that delaying is not compensated by the host (it is not reported as latency).
- [ ] Possible follow-ups: delay the sidechain instead/as well (so a kick can be pulled early); a dotted/triplet-free linear beat range; a "retrigger" mode that re-aligns the delay to the sidechain hit.
