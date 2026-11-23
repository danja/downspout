# Downspout TODO

Open work only. Completed builds are recorded in plugin docs; resolved lessons go to `AGENTS.md`.

## Validation queue

- [ ] Box in REAPER: audition the wooden-body resonances and extreme parameter ranges; confirm CC 1-4 on the configured MIDI channel, save/reopen every parameter plus Mix/Output/Bypass, and verify patch retention across activate/sample-rate changes. Core tests, VST3 build and a generated screenshot exist; live host listening and visual acceptance remain.
- [ ] Host passes for recent work: Fontana (SF2/SF3, WAV/Ogg slices, browser, save/reopen), Melevolve (live MIDI render/listening and timing-source observation; core host-BBT play edge now covered), Gate, Tilt, Polyscribe, Splay CC, Bassops input delay and new aux sidechain bus, Plank, Treatment, Prism, Mouth and NAM. Melevolve save/reopen and Fresco's silent-input default REAPER render already passed.
- [ ] Spliff in REAPER: audition the new 70 % Depth/Sensitivity defaults, Split Low/High and full Cut/Boost, then change the analyser source, save and reopen to confirm `analyser_source` and the 64 spectrum outputs. Host plumbing is fixed (ranges `{def,min,max}`, automatable hints, real `setParameterValue`, UI sync, source as appended parameter at index 83); ReaScript and a custom VST3 host probe confirm defaults, writes, audio processing and source-dependent spectra, but live spectrum/audio still needs the user's ears/eyes because headless Xvfb REAPER does not paint the plugin view.
- [ ] Audit `initParameter()` across plugins for `ParameterRanges` aggregate initialisation in the wrong `{def,min,max}` order (ghost, damiano, helterskelter, skream, chipper, etc.); fix and verify in REAPER before trusting their host automation.
- [ ] Save/reopen sweep for non-parameter state: chipper, damiano, ghost, helterskelter, skream, spliff, lifeform, luma, paunchlad. Check patch retention across activation and sample-rate changes for gremlin, gremlin-driver and flues-synth-driver.
- [ ] Sam: decide whether the unlicensed vendored engine may ship; validate both modes with a real transport and save/reopen; listen to upper-keyboard tuning, aliasing and Keys-mode speaking.
- [ ] Ear checks where only tests/builds exist: melgen and counterpointer Inertia, Silken, Skream Morph LP-HP, Damiano Linked/Split, voxmod, Sinevox, Beards, Pratt, Mouth, Bassops delay, Syrinx presets, NAM captures/IRs, Treatment bank/sweep.

## Plugin work

- [x] Box: portable wooden-structure physical model, stereo DPF/VST3 plugin, Magneto-style UI, CC 1-4 mapping, core tests, profile, catalog page and release/install wiring.
- [ ] Box: visually accept `docs/pages/assets/plugins/box.png` (960x720) from a first-time-user perspective; this session could not display the PNG, so check labels, hierarchy, contrast, clipping and response plot in an image-capable viewer before marking it final.
- [x] Melevolve UI: shorten Interact bars and add a top Favourite bar; replace per-bar OUT buttons with Fave buttons that choose which bar plays; keep only Interact and Settings tabs, moving compact status onto Interact.
- [x] Melevolve Settings: fix Line: Melody/Bass reversed UI and selection mapping so clicking a choice selects that line.
- [x] Melevolve transport: emit MIDI when REAPER plays in BBT/host-clock mode; cover stopped transport, loop boundaries, rewind and tempo/bar changes.
- [x] Melevolve UI: keep the `+` vote visible on carried-over rows using Melogen's outlined style.
- [x] Melevolve: accepted the fresh 920x700 screenshot after the carried-vote fix and copied it into the catalog.
- [x] Conductor: raise the block limit from 8 to 16 with a second row in the panel; append block 9-16 type parameters so saved host indices stay valid.
- [x] Conductor: accepted the fresh 960x820 screenshot showing all 16 blocks and copied it into the catalog.
- [x] Spliff redesign: separate Drift CC tab, half-length lower controls, upper 64-band Input/Output/Delta analyser, stronger Cut/Boost, audible Split Low/High, tests, docs and profile.
- [x] Spliff: accepted the 720x680 screenshot after the final source-switch placement and copied it into the catalog.
- [ ] Spliff: visually accept the fresh 720x680 capture after the analyser-visibility, parameter-overlap and 70 % default-text fixes (`/tmp/opencode/spliff-shots/spliff.png`), then copy it into the catalog. This model cannot view PNG pixels; acceptance remains with the user.
- [x] Spliff: raise Depth/Sensitivity defaults to 0.7 after a steady-tone probe showed the old 0.5/0.5 pair passed 98.9 % of the input; add a core bound test, rebuild the installed VST3 bundle and update profile/docs.
- [x] Spliff: expose `analyser_source` as an automatable parameter at index 83 so the UI switch reaches the DSP in REAPER; keep the state key for save/reopen and leave spectrum outputs at 19–82.
- [x] Fontana: persist the file browser directory as DPF state so it survives editor/focus loss; add a global Pan offset so hard-left SoundFont presets can be rebalanced (core test covers the audible effect).
- [ ] Refresh `docs/pages/assets/plugins/fontana.png` from the fresh 1280x800 capture and visually review the PLAYER mixer row (Pan label/value fit) before marking the screenshot accepted.
- [ ] Review `https://gardenfx.uk/v4/` for extensions and new-plugin ideas.
- [ ] PadGen: choose scope (pad synth vs MIDI generator) and build if distinct from Basilico, Canticle, Moka, Quartet, Strata, Monochord.
- [ ] MIDI filter/router (channel/note filter and remap); lower-priority ADT/doubler, Inflator, spectral compressor, dynamic tilt and channel strip.
- [ ] Add Launchpad support to `xoxolo`, reusing PadSeq's control scheme.
- [ ] Silken extras: breath reducer, dynamic resonance suppressor, formant-aware sibilance detector, look-ahead, oversampled drive/sparkle, mid/side de-essing and vocal limiter/rider.
- [x] Fresco: default Follow input level to 0 so the Tone source drones without audio input; verified by a REAPER offline render.
- [ ] Fresco: listen/look on an analyser; JPEG decoder (ask before vendoring); video frame sequence; GIF/WebP; time-stretch, bar-synced Length, floor/dither and live column plot; transient-preserving Input mode.
- [ ] Syrinx: tune Chirp/preset syllables, check audio-mode routing and latency, compare with Lyrebird, decide the old-project syllable default.
- [ ] NAM: CPU/Eco mode, input-level calibration, model-change crossfade, macOS/Windows build and a ctest wrapper probe when a second plugin needs it.
- [ ] Treatment: host-validate bank/sweep and CC 1-5; audition Ring cap and panel geometries.
- [ ] Plank: host-validate resonators, CPU and activation/sample-rate state; add cents tables for Harmonics, Hexany and Just.
- [ ] Prism: listen to the LFO/delay modulation (capture already accepted).
- [ ] Mouth phase 2: tempo-synced vowel sequencer using Prism's tempo plumbing.
- [ ] Harmonic-atlas: listen to Gravity and voice-leading; consider centre pull and a gravity bias for `cadence`.
- [ ] Generator follow-ups: audition Markov, Sprout, Conductor awareness and drumgen/xoxolo CA lanes; decide on undo/pass-through and a chord-graph walker.
- [ ] Audit sidechain port metadata: p-mix names inputs 3–4 "Sidechain" but, unlike Gate/Glue/Trio/Bassops/Interleaver, sets neither a port group nor `kAudioPortIsSidechain`, so VST3/CLAP hosts may expose them as a second main bus.
- [ ] Jig port of Sprout, Markov and Moka from Farelo (outside this repo; keep cores DPF-free).

## New plugin ideas

- [ ] Noise-reduction plugin: STFT gate/subtraction, learned/automatic noise profile, controls, musical-noise suppression, reported latency, stereo linking, Magneto UI, deterministic probes before DSP, docs/release wiring. Ask before reusing Prism/Qune STFT as shared code.
- [ ] Audio-driven string/comb (extended Karplus-Strong): damping, pick position, stiffness, MIDI/pitch tracking.
- [ ] Envelope-follower multimode filter (auto-wah).

## Recurring checks

- [ ] Read `INBOX.md` and the read-only `dim` tools; promote relevant tasks here and clear the inbox.
- [ ] After parameter/default/MIDI/routing changes: update `profile.ttl`, `plugins-summary.md`, tests and screenshots; rebuild the plugin's `-vst3` target and rerun its core tests.
- [ ] Run shell syntax checks and `scripts/check-bundle-lists.sh` when install/release wiring changes.
- [ ] Promote new recurring lessons from `MISTAKES.md` to `AGENTS.md`.
