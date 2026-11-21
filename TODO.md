# Downspout TODO

Open work only. Completed builds are recorded in plugin docs; resolved lessons go to `AGENTS.md`.

## Validation queue

- [ ] Host passes for recent work: Fontana (SF2/SF3, WAV/Ogg slices, browser, save/reopen), Melevolve (live MIDI render/listening and timing-source observation; core host-BBT play edge now covered), Gate, Tilt, Polyscribe, Splay CC, Bassops input delay, Plank, Treatment, Prism, Mouth and NAM. Melevolve save/reopen and Fresco's silent-input default REAPER render already passed.
- [ ] Save/reopen sweep for non-parameter state: chipper, damiano, ghost, helterskelter, skream, spliff, lifeform, luma, paunchlad. Check patch retention across activation and sample-rate changes for gremlin, gremlin-driver and flues-synth-driver.
- [ ] Sam: decide whether the unlicensed vendored engine may ship; validate both modes with a real transport and save/reopen; listen to upper-keyboard tuning, aliasing and Keys-mode speaking.
- [ ] Ear checks where only tests/builds exist: melgen and counterpointer Inertia, Silken, Skream Morph LP-HP, Damiano Linked/Split, voxmod, Sinevox, Beards, Pratt, Mouth, Bassops delay, Syrinx presets, NAM captures/IRs, Treatment bank/sweep.

## Plugin work

- [x] Melevolve UI: shorten Interact bars and add a top Favourite bar; replace per-bar OUT buttons with Fave buttons that choose which bar plays; keep only Interact and Settings tabs, moving compact status onto Interact.
- [x] Melevolve Settings: fix Line: Melody/Bass reversed UI and selection mapping so clicking a choice selects that line.
- [x] Melevolve transport: emit MIDI when REAPER plays in BBT/host-clock mode; cover stopped transport, loop boundaries, rewind and tempo/bar changes.
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
