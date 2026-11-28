# Downspout TODO

Open work only. Completed builds are recorded in plugin docs; resolved lessons go to `AGENTS.md`.

## Validation queue

- [ ] Box in REAPER: audition the wooden-body resonances and extreme parameter ranges, especially slow Size sweeps now that coefficient interpolation is gone; click Randomise and watch the panel values change; confirm CC 1-4 on the configured MIDI channel, save/reopen every parameter plus Mix/Output/Bypass, and verify patch retention across activate/sample-rate changes. Core tests, VST3 build and a refreshed screenshot exist; live host listening and visual acceptance remain.
- [ ] PadSeq in REAPER with a real Launchpad: confirm the programmer-mode handshake makes pads, top buttons and side triggers respond, then confirm the default `LED Out` = `SysEx` lights the Launchpad while a shared synth receives no channel-1 LED notes/CCs, including on Clear. Check that the UI `LED Out` selector switches to `Note+CC` and `Off`. Route generated notes (channel 4 by default) to the instrument. Core tests and the VST3 build pass; hardware validation remains.
- [ ] Visually accept and copy a fresh PadSeq screenshot into `docs/pages/assets/plugins/padseq.png` after the LED Feedback default changed to On, the LED Out selector was added and defaulted to `SysEx`, and the Page stepper/top buttons were fixed to mirror UI writes. A 980x720 capture is at `/tmp/opencode/padseq-page-shot/padseq.png`; pixel sampling verified the LED selector, top Clear button, Page stepper and top Page buttons, but image inspection was unavailable and it has not been copied into the catalog.
- [ ] Energise in REAPER: feed a drum break into inputs 1/2, arm Capture and confirm recording starts on the next host bar line with BPM locked to the DAW; then Analyse/Edit/Render and audition Audio Out Off/Pass/Render, A/B, BBT Loop on/off and MIDI Out on/off. Confirm the Demo path, every Class/Time/Velocity/Ghost edit, undo/redo, mute/solo masks, state save/reopen across activation and sample-rate changes, and that the refreshed pattern emits channel-10 note-on/note-off pairs that repeat with the BBT loop (with Export MIDI as a re-arm). Core tests and the VST3 build pass; live host transport/listening remains.
- [ ] Visually accept and copy the fresh Energise screenshot (1120x840, demo preloaded) from `/tmp/opencode/energise-shots/energise.png` into `docs/pages/assets/plugins/energise.png`; the catalog image is stale after the BBT/output-switch revision. Pixel sampling confirmed dark selected-button labels, an empty `AUDIO` gutter before the buttons, and table headers starting at their fixed column x positions, but this agent cannot view PNG pixels, so visual acceptance remains pending.
- [ ] Host passes for recent work: Fontana (SF2/SF3, WAV/Ogg slices, browser, save/reopen), Melevolve (live MIDI render/listening and timing-source observation; core host-BBT play edge now covered), Gate, Tilt, Polyscribe, Splay CC, Bassops input delay and new aux sidechain bus, Plank, Treatment, Prism, Mouth and NAM. Melevolve save/reopen and Fresco's silent-input default REAPER render already passed.
- [ ] p-mix in REAPER: confirm inputs 3-4 appear as a Sidechain aux bus instead of a second main input, and that Oppose responds to it. A VST3 factory probe confirms bus 1 is a 2-channel AUX named Sidechain; core tests and the VST3 build pass.
- [ ] Spliff in REAPER: audition the new 70 % Depth/Sensitivity defaults, Split Low/High and full Cut/Boost, then change the analyser source, save and reopen to confirm `analyser_source` and the 64 spectrum outputs. Host plumbing is fixed (ranges `{def,min,max}`, automatable hints, real `setParameterValue`, UI sync, source as appended parameter at index 83); ReaScript and a custom VST3 host probe confirm defaults, writes, audio processing and source-dependent spectra, but live spectrum/audio still needs the user's ears/eyes because headless Xvfb REAPER does not paint the plugin view.
- [ ] Verify in REAPER that host automation shows the corrected ranges/defaults for chipper, damiano, ghost, helterskelter and skream. A source scan and VST3 factory probe now confirm `{def,min,max}` metadata and the VST3 targets/core tests pass; only the live host check remains.
- [ ] Save/reopen sweep for non-parameter state: chipper, damiano, ghost, helterskelter, skream, spliff, lifeform, luma, paunchlad. Check patch retention across activation and sample-rate changes for gremlin, gremlin-driver and flues-synth-driver.
- [ ] Sam: decide whether the unlicensed vendored engine may ship; validate both modes with a real transport and save/reopen; listen to upper-keyboard tuning, aliasing and Keys-mode speaking.
- [ ] Ear checks where only tests/builds exist: melgen and counterpointer Inertia, Silken, Skream Morph LP-HP, Damiano Linked/Split, voxmod, Sinevox, Beards, Pratt, Mouth, Bassops delay, Syrinx presets, NAM captures/IRs, Treatment bank/sweep.
- [ ] Investigate the inbox report that many Downspout VST3 plugins fail to load in cracked Ableton 12 on Windows 10/Ryzen 7. Ask for the exact scan errors and compare with a clean licensed host before changing shared VST3 metadata.

## Plugin work

- [ ] Guardian: consider a crest-factor/headroom readout, per-band density and target-based auto ceiling (promoted from `INBOX.md`).
- [ ] Guardian in REAPER: audition the default-on clip interpolater on a deliberately hot signal (A/B the switch), check the reported latency of look-ahead plus 5 ms, sweep Auto drive / target reduction, Program release, 4x true-peak guard and the protection-density meter, then save/reopen. Core tests and the VST3 build pass.
- [ ] Guardian: visually accept the refreshed `docs/pages/assets/plugins/guardian.png` (960x780) from a first-time-user perspective. This model cannot view PNGs; pixel checks confirmed the sections render, the Activity title is not covered and the yellow transfer-curve marker is present at full plot height, but label fit, hierarchy and contrast need human eyes.
- [ ] Voxmod: audition the new convolution engine in a host with an instrument on 1/2 and a moving voice/texture on 3/4; confirm the ring mod now multiplies by 3/4 when that bus is routed (including with a slow vocoder Attack), then check the `1/2 in` / `3/4 in` meters, sweep the three Levels, Conv Mix and both feedback controls, and save/reopen to confirm the new state keys. Core tests (vocoder source/carrier, external/internal ring carrier, stereo convolver, oscillator sample-rate and input-bus meter pins), the wrapper probe (normal + ASan), VST3 build and state audit pass; the catalog screenshot was refreshed after the carrier/UI-label changes (800x700), but this model cannot view PNGs, so visual acceptance remains pending.

- [ ] Box: visually accept the refreshed `docs/pages/assets/plugins/box.png` (960x720) from a first-time-user perspective; this session could not display the PNG, so check labels, hierarchy, contrast, clipping, the smaller Randomise button and the response plot in an image-capable viewer before marking it final.
- [ ] Spliff: visually accept the fresh 720x680 capture after the analyser-visibility, parameter-overlap and 70 % default-text fixes (`/tmp/opencode/spliff-shots/spliff.png`), then copy it into the catalog. This model cannot view PNG pixels; acceptance remains with the user.
- [ ] Refresh `docs/pages/assets/plugins/fontana.png` from the fresh 1280x800 capture and visually review the PLAYER mixer row (Pan label/value fit) before marking the screenshot accepted.
- [ ] Cadence in REAPER: after changing Segment to Beat or Bar, press Learn again and confirm the progression changes rather than staying constant; check save/reopen of the Learn counter and that the host exposes Learn over 0..1048576. Core tests cover the same segment counts with the zero-meter fallback and the Learn-counter retrigger.
- [ ] DrumGen in REAPER: route MIDI out to DrumKit, press New/Mutate/Fill and confirm emitted hits change (the `UPDATED`/`Gen N` pill should flash); check each Pattern Mix slider has an audible effect, the counter parameters expose 0..1048576, and save/reopen retains the pattern and counters. Core tests compare emitted `(note, velocity)` pairs for every Mix control. A DPF wrapper probe confirms `new`/`mutate`/`fill` are automatable integers over 0..1048576 with no trigger flag and round-trip `setParameterValue`/`getParameterValue`; the UI click path and host automation still need a live host.
- [ ] DrumGen in REAPER: set Conductor Ch and confirm CC 21-23 move the Density/Variation/Vary sliders through the new `status_*` outputs; check the read-only outputs in the host automation list. Appended output params, UI mirroring, profile/docs, a UI mapping assertion and a wrapper probe injecting the CCs are done; only the live host check remains.
- [ ] DrumGen: after that host check, consider a Hierophant-style odd-meter generator (Seed/Density/Variation/Lilt plus an editable step grid) in the portable core, not a new plugin.
- [ ] Markov: refresh `docs/pages/assets/plugins/markov.png` after the learned-count readout moved to the output parameter, and visually accept it. A headless pixel probe confirmed the first digit changes 0→7 after a rendered MIDI line, but this model cannot view PNGs.
- [ ] Cadence: capture a fresh screenshot into the catalog after the Learn button became an ordinary integer request counter. `scripts/capture-plugin-screenshots.sh` already includes `cadence:cadence`; inspect the image at full resolution before accepting.
- [ ] DrumGen: visually accept the fresh 1024x940 capture at `/tmp/opencode/drumgen-live-shots/drumgen.png` (Pattern Mix relabel, `UPDATED`/`Gen N` indicator, spacing rebalance and live status mirroring), then copy it into `docs/pages/assets/plugins/drumgen.png`. This session cannot view PNGs, so acceptance remains with the user.
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
- [ ] Six-circuit saturator wheel (Garden FX Kiln): continuously blend adjacent circuits, LEAN by playing level, slow MOTION, SIDEBAR peaks and harmonics-only tone shaping; write the response probe before DSP.
- [ ] Ducking compressor with a filtered external key (Garden FX DUK34): check against Gate/Glue first, then add threshold/ratio/recovery and sidechain audition.
- [ ] Phase/time-align utility (Garden FX Align): per-channel delay and polarity with a correlation readout; verify with a known impulse pair before UI.
- [ ] Tape colour processor (Garden FX Pinch/Tape Push): saturation, wow/flutter, head bump, auto-bias and reported latency.

## Recurring checks

- [ ] Read `INBOX.md` and the read-only `dim` tools; promote relevant tasks here and clear the inbox.
- [ ] After parameter/default/MIDI/routing changes: update `profile.ttl`, `plugins-summary.md`, tests and screenshots; rebuild the plugin's `-vst3` target and rerun its core tests.
- [ ] Fix `scripts/capture-plugin-screenshots.sh`: `jack_wait -c` prints "not running" but exits 0, so the script skips starting its dummy JACK server and capture times out. Workaround used here: start `jackd -r -d dummy -r 48000 -p 256` manually and set `DOWNSPOUT_SCREENSHOT_START_JACK=0`. Shared script left unchanged pending approval.
- [ ] Run shell syntax checks and `scripts/check-bundle-lists.sh` when install/release wiring changes.
- [ ] Promote new recurring lessons from `MISTAKES.md` to `AGENTS.md`.
