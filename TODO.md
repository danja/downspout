# Downspout TODO

Open items only. Finished work is recorded in the plugin docs and `MISTAKES.md`.

review `docs/lynx-survey.md`

## New plugin proposals (from the DSP-Ports-JSFX survey, 2026-11-03)

- [ ] **Vocal extras left out of `silken`** (to keep its panel readable; each could be a plugin or a later addition): breath reducer (a gate/expander keyed on unvoiced
      low-level HF between words); dynamic resonance suppressor (multi-band version of the harshness tamer); vocal doubler / ADT and stereo widener (Chorale is close
      for the chorus part); a formant-aware sibilance detector; look-ahead for the de-esser and plosive guard (needs latency reporting); oversampled drive and sparkle;
      mid/side de-essing; a dedicated vocal limiter / clipper at the end of the chain; auto-gain / vocal rider (slow level riding before the compressor).
- [ ] **Gate** (ZamGateX2 reference, GPL, clean-room): standalone audio noise gate with sidechain.
- [ ] **MIDI filter / router** (Spaghetti Junction, x42 midifilter, GPL): channel/note filter and remap.
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
- [x] **Survey `https://github.com/Ka1-S/LYNX`** (Farelo task t20259e38295b): reported candidates and overlap in
      `docs/lynx-survey.md`. Best small gaps are a level-independent transient designer, a stereo matrix/enhancer
      and a cheap fixed-band denoiser; the largest true gap is a sample/SF2 player. Most LYNX mixing engines overlap
      `guardian`, `glue`, `trio`, `octet`, `silken`, `heavyverb`, `loopdelay`, `qune` or `sinevox`.

## From INBOX (2026-11-17)

### New plugins

- [ ] **Stereo enhancer** (new plugin, from INBOX): design and build a stereo widener/enhancer.
      Consider the overlap with Chorale's stereo chorus and any existing widening controls.
      Follow the new-plugin checklist.
- [x] **PadSeq** (built 2026-11-18 as `padseq`, from INBOX): a paged 8x8 grid sequencer based around a Launchpad.
      Grid-based sequence editor like a standard MIDI editor, but supporting alternate scales, including
      mapping to General MIDI drums. Time on the x axis (left to right), note number on the y axis. Based around a paged
      8x8 grid, with sequence length 1 to 32 steps, default 16. Use `xoxolo` (written "Xololo" in INBOX)
      and `luma` as references, and the common scales list in `docs/scales.md`. The plugin can be used
      without a Launchpad, but its UI has controls corresponding to those of the Launchpad. Follow the
      new-plugin checklist.
      - Top controls, left to right: pattern view down a step; pattern view up a step; previous page;
        next page; previous scale; next scale; randomise (modify the current pattern if there is one,
        otherwise generate a new one); clear pattern & reset (panic).
      - The controls along the right column correspond to notes in the grid. While running, pressing a
        key sends the corresponding note-on message, with note-off timed to the grid step.
      Built: `plugins/padseq` with 384 passing core checks, a clean `padseq-vst3` build and a VST3 bundle.
      Host load and save/reopen were validated in an isolated REAPER profile: all 16 programmed cells,
      Note Map and LED Feedback survived the round trip. Follow-ups fixed the reported axis bug (time
      now scans left to right and note rows run vertically), late-block note-off timing (input is
      consumed before boundary scheduling), and the LED artifact: feedback is now SysEx-only (no
      channel-1 note-ons/CCs) and Clear blacks out all LEDs and holds them dark until the next edit
      or run. Realtime transport and CC/Launchpad-LED validation remain blocked in the headless
      session (the transport does not advance even with audio running).
      The updated UI was recaptured to `/tmp/opencode/padseq-screenshot/padseq.png` and dimension-checked
      at 980x720, but this model cannot view PNG pixels, so visual acceptance and copying into
      `docs/pages/assets/plugins/padseq.png` remain pending.
- [ ] Visual acceptance of Conductor's fresh capture is pending: the eight-block editor was recaptured
      to `/tmp/opencode/conductor-screenshot/conductor.png` and dimension-checked at 960x820, but this
      model cannot view PNG pixels, so copying it into `docs/pages/assets/plugins/conductor.png`
      remains pending.
- [ ] **Launchpad support for `xoxolo`** (from INBOX, written there as "xololo"): add Launchpad support,
      reusing the PadSeq control scheme where it makes sense.

## From INBOX (2026-11-18)

- [ ] **Level-independent transient designer** (from INBOX): add a `spliff` mode based on LYNX's
      *Strike Shaper* / SPL Transient Designer. Attack ±15 dB and Sustain ±24 dB from two envelopes
      with no threshold; keep the level-independent behaviour, which is the point.
- [x] **Editable Conductor form** (from INBOX, done 2026-11-18): up to 8 blocks of types Null,
      Intro, Theme, Develop, Break, Reprise and Coda. No extra types were added: Null covers
      rests and the other six cover the requested musical roles. `block_count`, per-block type
      parameters and next-bar jumps are host-automatable; receivers that decode CC 20 share
      `include/downspout/conductor_protocol.hpp` with the editor.


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
- [ ] GIF, WebP.
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

- Last full sweep 2026-11-17: zero warnings in all 187 plugin core source files under `-Wall -Wextra`
  (using each plugin's CMake include flags; nam no longer reports vendored-header warnings in this sweep).
  The changed UI wrapper targets also rebuilt cleanly with CMake's `-Wall -Wextra` during the Reroll work.
  Previous full sweep 2026-11-06 included the new shared `voice_leading.hpp` and the
  markov/bassgen/cadence/drumgen changes.
- [x] `campione` loop end: keep the WAV `smpl` convention (both markers inclusive) and wrap the
      cursor at `loopEnd + 1`, including the marker frame in playback and the crossfade. Added a shared
      `loopFrameCount()` so the engine, loop-point snapping, state updates and MCP edits all agree;
      deterministic tests cover a plain loop and a four-frame crossfade. State serialization was
      already inclusive, so saved patches keep their existing loop numbers and now play the last frame.

## Treatment

- [ ] **Host validation in REAPER** (Farelo task t545a1867c482, "multiple panels / special effect"):
      the multi-panel bank, Ring mode and geometry sweep are built; validate in a host (save/reopen,
      synced sweep against a real transport, CC 1-5). Not yet auditioned by ear.
- [ ] Bank/sweep audition: is the Ring cap (+9 dB) and the default panel 2-4 geometries right?

## Plank

- [ ] Host validation in REAPER, especially the resonator engines: CPU load, audio out, and
      the patch surviving playback start and a sample-rate change (the fixes are covered by
      offline tests only, and the original "no audio" symptom was never reproduced offline).
- [x] Plinky's other integer scales (Hirajoshi, Insen, Iwato, Minyo, major and minor triads,
      fifths, Romanian) were appended at Plank local ordinals 24-31 on 2026-11-17. The
      canonical rows 25-32, tests, wrapper labels, docs, profile and screenshot were updated.
      The microtonal ones (Harmonics, Hexany, Just) still need a cents-based table, which the
      current integer ladder cannot express.
- [ ] Visual acceptance of Plank's fresh capture is pending: the 752x700 screenshot was recaptured and
      checked programmatically, but this session cannot view images. Inspect
      `docs/pages/assets/plugins/plank.png` at full resolution when image review is available.

## Other

- **Bore** host audition in REAPER: check the playable range and level, confirm
  Pressure/Embouchure/Bell/Breath, Reed Instability, and Register Capture are
  musically useful (CC 1-6); try upper-mode capture with high pressure and tight
  embouchure, and save/reopen a project. The core suite and VST3 build pass; it
  has not yet been heard in a host.
- `skream` **Morph LP-HP** (bipolar, default off): not yet listened to in a host; try it with Drift
  on Cutoff. Decisions worth a listen: the morph weight follows cutoff position linearly (t = 0.5
  is a notch at the cutoff), and it only reshapes the forward filter, not the Scream feedback
  high-pass.
- **Damiano Linked/Split stereo** (`plugins/damiano/docs/porting-notes.md`): not yet auditioned in a
  host: try Split with Soft left and Fuzz right, and two Drift lanes on CC Drive / CC Drive R.
  Verify an old project (pre-split state) loads Linked. Open: "shape" is the stepped waveshaper
  mode; a continuous shape (asymmetry/bias) would be new DSP. Tone and Folds are not CC-controlled.
- `voxmod`: host validation in REAPER still pending.
- `sinevox`: host validation in REAPER pending, in particular (1) that the host routes
  MIDI out from an effect, (2) the 4-in layout (speech 1/2, carrier 3/4) and (3) that
  saved projects restore the patch. Not yet auditioned on real speech: high voices
  (f0 > ~300 Hz) may lock onto harmonics; fricatives give wide unsteady tracks. Ideas:
  pitch-aware analysis window, a fixed per-formant frequency range, an optional noise
  source for unvoiced frames in vocoder mode, panel following CC changes (needs an
  output mirror parameter).
- `keyframe`: the leash diagram is static at unity rates because drift is genuinely ~0 there;
  a splice halo was added so it reads live during crossfades. If it still reads dead at
  defaults, the diagram needs a rethink, not another lamp.
- **Beards** host validation and follow-ups: load `beards.vst3` in REAPER, route Drift (CC 1-4)
  and Conductor (ch 16) into it, and listen: the DSP is a port but has only been checked
  by core tests and a headless UI capture. Not ported from Beads: delay (multitap) mode,
  CV/gate inputs, wow and flutter, 24 kHz Tape rate. The reverb is a new Griesinger tank,
  not Beads' `fx_engine`. See `plugins/beards/docs/design.md`.

## Pratt

- **Verify in a host (REAPER):** load, play notes, filter an audio track, save/reopen state.
  Nothing has been listened to. Also confirm the host accepts an instrument with audio inputs.
- Listen to drum levels (normalised per hit, consistent at ~0.15); check voice-stealing clicks.
- GM Program mode queues a warm-up per Program Change (last 8 voices); host-check that
  switching programs on several channels no longer glitches on first notes. Offline-tested only.
- Catalogue screenshot shows Synth mode (filter dimmed); consider a Synth + Filter capture.
- **Timbre Index** (separate fixed filter chain, default 1 = off) is tested; not yet listened to.
  Open: it is limited to one index <= 8192 (degree <= 13), so very dark voices still lean on
  Rolloff; a second chain or a larger limit would need `kMaxSections` raised and a warm-up that
  touches only the indices in use (see `plugins/pratt/docs/high-base.md`). Filter mode keeps
  A x B capped at 8192.

## Generators and MIDI tools (host and listening checks, small follow-ups)

- **melgen Inertia** and **counterpointer Inertia** (scoring term; Counterpoint mode only, not Bass
  Descend): not yet listened to in a host.
- **Retune** (`plugins/retune/`): host validation, a catalogue screenshot with a real `.scl` loaded (the
  standalone shows 12-TET), `.kbm` support, saving scale contents with the session (only the path is
  saved), and an audition against a per-channel-bend synth.
- **Per-channel pitch bend** (needed for Retune): done for canticle, moka, floozy, syrinx, basilico,
  gremlin, campione, mosaic, strata, quartet, monochord (pratt already bends per channel; plank, magneto and drumkit are not
  applicable). Remaining: audition them with Retune in a host, confirm pratt's default Bend Range suits
  Retune's 2 st, and add a Bend Range control where a synth has a fixed range, noting it in each README.
- **Sprout**: host validation, a rule editor (grammars are built in), listening to which grammars sound
  good at which Step size; its MIDI input (held-notes pitch source, Drift CC 1-4, Conductor CC 21-24) is
  not yet auditioned (try harmonic-atlas into it, Pitch source = Held). Open: MIDI is applied at block
  start, not sample-accurately; no pass-through of incoming MIDI.
- **drumgen** (`Generation every`) and **CA lanes in drumgen and xoxolo**: not yet listened to (try Rule 90
  and 30 on Hats; mark the hat and percussion lanes in xoxolo). Open: xoxolo's grid shows the programmed
  pattern, not the evolved row now playing.
- [x] **Magneto look and feel:** checked the shared-header rebuilds. Retune's fresh capture is pixel-identical to
  the catalogue image; Markov's was stale (the Clear matrix / Randomise buttons were missing) and has been refreshed.
  Sprout's fresh capture is pixel-identical to the one recaptured for its new Reroll button. Older generator plugins
  still use `GenerativePanelUI` (mosaic, polymeter, etc.); restyling them remains an open choice.
- **Gravity** is in `harmonic-atlas`. `cadence` (learned harmony) has not had it; its progression model is
  different, so decide whether a gravity bias on the learned transitions makes sense.
- **Atlas voice-leading** is local to `harmonic-atlas`. Listen to it; the chords sit low (around C3-C4)
  because only the bottom and top of the range are penalised, so consider a centre pull.
- **Markov**: not yet auditioned in a host (each style, Order 2 after learning a line, Learned mix at 50 %).
  Open: learning assumes one melodic line (chords blur the counts); the matrix editor has click-to-cycle,
  drag-to-paint and right-click-to-zero but no undo; no MIDI pass-through; the editor does not move while a
  CC drives a control.
- **Conductor awareness** (lifeform, luma, polymeter, xoxolo, counterpointer, cadence, arpgen, m-mix; off by
  default) is not yet auditioned in a host. Still without it: `mnemosyne` (only Novelty and a seed re-roll
  would fit, and its core takes parameters const, so it needs wrapper-level handling), `sidecar` (its wrapper
  is tangled up with the server path), `tuney-vst`.
- **Jig port (from Farelo, `#jigdaw`):** port Sprout, Markov and Moka from Downspout to Jig format. Lives
  outside this repo; keep their portable cores free of DPF code so they move cleanly.
- Later candidate: chord-graph walker. A shared voice-leading helper in generative-common is only worth it
  if a second plugin needs it (shared code, needs approval).

## New plugins from the oxide-studio review (October 2026)

All seven are built, wired and tested (core suites under ASan/UBSan where noted in their `docs/design.md`).
None has been listened to or run in a host. Remaining per plugin:

- **ladder**: listen (is Bass Comp 50 % the right default? the defaults sit 3.5 dB below unity); aliasing at
  high drive is not measured. External review (Nov 2026): the Huovilainen + Mystran cheap-ZDF design is
  judged sound and the tanh approximation good, but self-resonance pitch is expected to go flat under
  heavy gain, and coefficients are smoothed linearly per-frame — spiky with variable host block sizes —
  instead of slew-limiting the base parameters. Consider slew limiting and measure resonance pitch
  tracking at high drive.
- **glue**: listen (is the auto-release table 40-800 ms / 8 dB depth right? the 10 ms RMS window?);
  check in a host that Look-ahead's reported latency is compensated.
- **octet**: listen to and look at the new spectrum analyser in a host (Off/Pre/Post; does the 40 dB/s fall and the 0 dB-at-full-scale scale read well? is a 4.5 dB/oct tilt wanted?); the CC band mapping (Frequency/Gain/Q of one band + Output) is my choice: confirm it suits a Drift lane.
- **trio**: listen (the OTT defaults are strong; the detector decay times are my choice); check the sidechain
  routing in a host.
- **strata**: listen; oversampled warps (Bend -42.6 dB, Sync -38.5 dB, Fold -33.1 dB aliasing at note 60,
  amount 0.7); table import; a modulation matrix; Fold + Sync
  worst case is about 90 % of a core.
- **quartet**: listen; user waveform (the source's 16-harmonic one); per-operator key and velocity scaling;
  4x oversampling for bright FM at the top of the keyboard.
- **monochord**: listen; sympathetic strings / several strings per note; frequency-dependent T60; bow noise.
- Decided not to build (revisit if wanted): **Electric piano** and **Collision** (a few modal filters each,
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

- remove tasks that have been done from this file
- check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
- if an issue in MISTAKES.md has been fully resolved, remove it from the file
- for new material, check test coverage
- ensure README.md and docs are up-to-date
- read INBOX.md and fold tasks in here; check `dim` for Farelo tasks relevant to Downspout (read-only)

## Prism

- [ ] **Listen to it.** Core tests only; nothing has been auditioned or run in a host.
- [ ] **Visual acceptance of the LFO panel:** the fresh capture was checked for size and text bounds, but this agent
      cannot view images, so a human should confirm the layout at full resolution.
- [x] LFO modulation of delay/feedback: added a free-running 0.05-10 Hz oscillator with separate Delay Depth and
      Feedback Depth controls (both default to zero, so existing patches are unchanged). Appended after the band block
      so saved parameter IDs stay stable. Core tests cover delay at both extremes, feedback-tail change, rate inertness
      and phase reset; profile, docs, UI and screenshot updated. The LFO is not bar-locked, like the existing tempo sync.

## Filter-plugin ideas (November 2026 brainstorm)

Mouth (vowel/formant morph filter) is built (core, VST3, tests, screenshot, docs); the rest are not started. Design for Mouth: `plugins/mouth/docs/design.md`.

- [ ] **Audio-driven string / comb (extended Karplus-Strong)**: feedback comb with damping low-pass, pick-position comb and stiffness all-pass chain (clean string -> inharmonic bell -> metal), tuned from MIDI or tracked pitch. Closest existing plugins: `resonance-garden`, `spense`; build after the other two. References: CCRMA "Extended Karplus-Strong" (https://ccrma.stanford.edu/~jos/pasp/Extended_Karplus_Strong_Algorithm.html).
- [ ] Envelope-follower multimode filter (auto-wah). Easy, but a crowded category.
- [ ] **Mouth: listen to it** and try it in a host (nothing auditioned). Check Voice switching for
      clicks on a loud sustained source.
- [x] Mouth vowel table verified against the Csound 7 Manual appendix "Formant Values" on
      2026-11-17. All 20 bass/tenor/alto/soprano x A/E/I/O/U rows match after correcting Tenor O
      bandwidths to 70/80/100/130/135 Hz; the core test now holds the full independent reference.
- [ ] Mouth phase 2: tempo-synced vowel sequencer (step list of vowel corners), reusing Prism's tempo plumbing.

## Noise-reduction plugin (idea, November 2026; not started)

Based on MOTU's Arboretum Ionizer (https://motu.com/products/motuaudio/plug/arboretum/ion.html): a multiband
spectral processor with 512 gated bands per channel, linear-phase, used for noise reduction, EQ, dynamics,
spectral morphing and keying. Aim for the noise-reduction side first; it is steady-state broadband noise
(hiss, hum, HVAC, camera noise) removed in real time without the chirps, flanging and ringing that cheap
spectral gates leave behind. Build from the general technique (STFT, per-band noise estimate, gain
smoothing), not from the product.

- [ ] **Core**: STFT analysis/resynthesis (2048-4096 FFT, 75 % overlap) with a per-bin noise-floor estimate and a per-bin gain (spectral gate / subtraction). Reuse the Prism/Qune STFT engine if it fits (shared code, needs approval before touching); otherwise keep it plugin-local.
- [ ] **Noise profile**: a _Learn_ mode that averages the spectrum of a noise-only passage into a fingerprint, plus an automatic mode that tracks the floor (minimum statistics) with no learning pass. The profile is state saved with the project (not a parameter; see `scripts/check-plugin-state.sh`) and visible in the panel.
- [ ] **Controls**: reduction amount (dB), threshold offset, attack/release or time smoothing, frequency smoothing, and a band-limit so hum (50/60 Hz and harmonics) can be handled separately from hiss.
- [ ] **Musical-noise suppression** is the hard part and the selling point: smooth the gain over time and frequency, and decide per bin with a decision-directed estimate; the goal is no tonal "chirps" when the signal is only noise. Write the probe before the DSP (CLAUDE.md "measure before building"): white noise in -> residual spectrum flatness and a tonal-artifact measure out, and a speech-like signal with noise -> noise reduced by the stated dB without damaging the signal beyond a bound.
- [ ] **Latency**: report the FFT latency to the host; test that it is compensated, and that block-size changes do not move it.
- [ ] **Stereo**: link the two channels' gains (default) so the image does not wander; optionally process mid/side.
- [ ] **UI**: Magneto look and feel; spectrum display with the noise fingerprint, the threshold curve and the gain reduction; a _Listen to removed noise_ (difference) switch to hear what is being taken out.
- [ ] Tests: deterministic core tests (`downspout_add_core_test`), no allocation on the audio path, denormal-safe, sample rates 44.1-192 kHz, `activate()` keeps the profile.
- [ ] Later, from the same product: spectral keying (one input's spectrum gates or ducks another, via sidechain) and frequency morph (impose one sound's spectral curve on another). Both overlap `prism`, `quefrency` and `bassops`; decide after the noise reduction exists.
- [ ] Docs per the new-plugin checklist (install/release lists, Pages product page and group in `_data/groups.yml` (probably `dynamics` or `spectral`), plugins-summary, screenshot).

## Bassops input delay (added November 2026)

- [ ] **Listen and try in a host.** The two delay sliders are tested in the core (exact shift, tempo lock, crossfade, a bass note held behind a kick) and the panel and status read-out were driven through the standalone, but nothing has been auditioned or run in a DAW. Check: the beat slider follows REAPER's tempo changes; the delay feels right against a real kick; 10 ms crossfade is inaudible while dragging; automation of `delay_beat` / `delay_ms`; that delaying is not compensated by the host (it is not reported as latency).
- [ ] Possible follow-ups: delay the sidechain instead/as well (so a kick can be pulled early); a dotted/triplet-free linear beat range; a "retrigger" mode that re-aligns the delay to the sidechain hit.
