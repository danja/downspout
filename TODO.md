# Downspout TODO

Open work only. Completed builds are recorded in plugin docs; resolved lessons go to `AGENTS.md`.

## Validation queue

- [ ] Box in REAPER: audition the wooden-body resonances and extreme parameter ranges, especially slow Size sweeps now that coefficient interpolation is gone; click Randomise and watch the panel values change; confirm CC 1-4 on the configured MIDI channel, save/reopen every parameter plus Mix/Output/Bypass, and verify patch retention across activate/sample-rate changes. Core tests, VST3 build and a refreshed screenshot exist; live host listening and visual acceptance remain.
- [ ] Host passes for recent work: Fontana (SF2/SF3, WAV/Ogg slices, browser, save/reopen), Melevolve (live MIDI render/listening and timing-source observation; core host-BBT play edge now covered), Gate, Tilt, Polyscribe, Splay CC, Bassops input delay and new aux sidechain bus, Plank, Treatment, Prism, Mouth and NAM. Melevolve save/reopen and Fresco's silent-input default REAPER render already passed.
- [ ] p-mix in REAPER: confirm inputs 3-4 appear as a Sidechain aux bus instead of a second main input, and that Oppose responds to it. A VST3 factory probe confirms bus 1 is a 2-channel AUX named Sidechain; core tests and the VST3 build pass.
- [ ] Spliff in REAPER: audition the new 70 % Depth/Sensitivity defaults, Split Low/High and full Cut/Boost, then change the analyser source, save and reopen to confirm `analyser_source` and the 64 spectrum outputs. Host plumbing is fixed (ranges `{def,min,max}`, automatable hints, real `setParameterValue`, UI sync, source as appended parameter at index 83); ReaScript and a custom VST3 host probe confirm defaults, writes, audio processing and source-dependent spectra, but live spectrum/audio still needs the user's ears/eyes because headless Xvfb REAPER does not paint the plugin view.
- [ ] Verify in REAPER that host automation shows the corrected ranges/defaults for chipper, damiano, ghost, helterskelter and skream. A source scan and VST3 factory probe now confirm `{def,min,max}` metadata and the VST3 targets/core tests pass; only the live host check remains.
- [ ] Save/reopen sweep for non-parameter state: chipper, damiano, ghost, helterskelter, skream, spliff, lifeform, luma, paunchlad. Check patch retention across activation and sample-rate changes for gremlin, gremlin-driver and flues-synth-driver.
- [ ] Sam: decide whether the unlicensed vendored engine may ship; validate both modes with a real transport and save/reopen; listen to upper-keyboard tuning, aliasing and Keys-mode speaking.
- [ ] Ear checks where only tests/builds exist: melgen and counterpointer Inertia, Silken, Skream Morph LP-HP, Damiano Linked/Split, voxmod, Sinevox, Beards, Pratt, Mouth, Bassops delay, Syrinx presets, NAM captures/IRs, Treatment bank/sweep.

## Plugin work

- [ ] Box: visually accept the refreshed `docs/pages/assets/plugins/box.png` (960x720) from a first-time-user perspective; this session could not display the PNG, so check labels, hierarchy, contrast, clipping, the smaller Randomise button and the response plot in an image-capable viewer before marking it final.
- [ ] Spliff: visually accept the fresh 720x680 capture after the analyser-visibility, parameter-overlap and 70 % default-text fixes (`/tmp/opencode/spliff-shots/spliff.png`), then copy it into the catalog. This model cannot view PNG pixels; acceptance remains with the user.
- [ ] Refresh `docs/pages/assets/plugins/fontana.png` from the fresh 1280x800 capture and visually review the PLAYER mixer row (Pan label/value fit) before marking the screenshot accepted.
- [ ] Cadence in REAPER: after changing Segment to Beat or Bar, press Learn again and confirm the progression changes rather than staying constant; check save/reopen of the Learn counter and that the host exposes Learn over 0..1048576. Core tests cover the same segment counts with the zero-meter fallback and the Learn-counter retrigger.
- [ ] DrumGen in REAPER: route MIDI out to DrumKit, press New/Mutate/Fill and confirm emitted hits change (the `UPDATED`/`Gen N` pill should flash); check each Pattern Mix slider has an audible effect, the counter parameters expose 0..1048576, and save/reopen retains the pattern and counters. Core tests compare emitted `(note, velocity)` pairs for every Mix control. A DPF wrapper probe confirms `new`/`mutate`/`fill` are automatable integers over 0..1048576 with no trigger flag and round-trip `setParameterValue`/`getParameterValue`; the UI click path and host automation still need a live host.
- [ ] DrumGen: remember Conductor CC 21-23 overwrite DSP density/variation/vary without updating the UI; decide whether to add output status parameters or document the one-way mapping in a host probe.
- [ ] Markov: refresh `docs/pages/assets/plugins/markov.png` after the learned-count readout moved to the output parameter, and visually accept it. A headless pixel probe confirmed the first digit changes 0→7 after a rendered MIDI line, but this model cannot view PNGs.
- [ ] Cadence: capture a fresh screenshot into the catalog after the Learn button became an ordinary integer request counter. `scripts/capture-plugin-screenshots.sh` already includes `cadence:cadence`; inspect the image at full resolution before accepting.
- [ ] DrumGen: capture a fresh screenshot after the Pattern Mix relabel, `UPDATED`/`Gen N` indicator and spacing rebalance (`/tmp/opencode/drumgen-shots-v2/drumgen.png`); inspect at full resolution (layout, contrast, clipped labels) before copying into `docs/pages/assets/plugins/drumgen.png`.
- [ ] Review `https://gardenfx.uk/v4/` for extensions and new-plugin ideas.
- [ ] PadGen: choose scope (pad synth vs MIDI generator) and build if distinct from Basilico, Canticle, Moka, Quartet, Strata, Monochord.
- [ ] MIDI filter/router (channel/note filter and remap); lower-priority ADT/doubler, Inflator, spectral compressor, dynamic tilt and channel strip.
- [ ] Add Launchpad support to `xoxolo`, reusing PadSeq's control scheme.
- [ ] Silken extras: breath reducer, dynamic resonance suppressor, formant-aware sibilance detector, look-ahead, oversampled drive/sparkle, mid/side de-essing and vocal limiter/rider.
- [ ] Fresco: listen/look on an analyser; JPEG decoder (ask before vendoring); video frame sequence; GIF/WebP; time-stretch, bar-synced Length, floor/dither and live column plot; transient-preserving Input mode.
- [ ] Syrinx: tune Chirp/preset syllables, check audio-mode routing and latency, compare with Lyrebird, decide the old-project syllable default.
- [ ] NAM: CPU/Eco mode, input-level calibration, model-change crossfade, macOS/Windows build and a ctest wrapper probe when a second plugin needs it.
- [ ] Treatment: host-validate bank/sweep and CC 1-5; audition Ring cap and panel geometries.
- [ ] Plank: host-validate resonators, CPU and activation/sample-rate state; add cents tables for Harmonics, Hexany and Just.
- [ ] Prism: listen to the LFO/delay modulation (capture already accepted).
- [ ] Mouth phase 2: tempo-synced vowel sequencer using Prism's tempo plumbing.
- [ ] Harmonic-atlas: listen to Gravity and voice-leading; consider centre pull and a gravity bias for `cadence`.
- [ ] Generator follow-ups: audition Markov, Sprout, Conductor awareness and drumgen/xoxolo CA lanes; decide on undo/pass-through and a chord-graph walker.
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
