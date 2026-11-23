# Mistakes log

New, unresolved entries only. Resolved lessons are promoted to `AGENTS.md` and removed from here.

Format: what happened, root cause, prevention.

## Open

## spliff: Split Low/High were inaudible (2026-11-23)
- **What happened:** moving either split produced a RMS difference of about 1e-5 in a broadband probe, and the peak was unchanged.
- **Root cause:** all three detector bands saturated to the same gain, so which band a frequency landed in made no practical difference. The single-pole crossovers also moved energy only slightly between them.
- **Prevention:** cascaded each crossover to two poles and gave the bands fixed 0.35/1.0/0.45 detector weights. Tests now assert the observable rendered-audio difference for a broadband burst and for a 500 Hz tone whose band changes with Split Low, rather than inspecting intermediate gains.

## spliff: Boost used the slow release to rise (2026-11-23)
- **What happened:** full Boost changed a short transient's peak by only about 1.4x while Cut clearly reduced it; Boost was much weaker than the manual-style setting implied.
- **Root cause:** gain smoothing used `target < gain` to choose the fast coefficient, which is correct for Cut but made Boost approach its target through the Decay-controlled release.
- **Prevention:** choose the fast coefficient whenever the target moves away from unity in either direction, and keep the slow coefficient for the return. The strength test now asserts a full-Boost transient peak above 4x dry.

## spliff: Decay was masked by the reference envelope (2026-11-23)
- **What happened:** changing Decay from 0 to 1 left the tail level identical inside the measured windows even though the band gains differed.
- **Root cause:** the detector's slow reference floor attacked in 10 ms and released in 250 ms, so it changed more slowly than the Decay-controlled fast envelope and hid its effect in the ratio.
- **Prevention:** use a 2 ms reference attack and 30 ms release, both shorter than the Decay tail. The test now renders a burst and asserts that the long-Decay tail is measurably lower than the short-Decay tail.

## spliff: analyser source and spectrum parameters shared indices (2026-11-23)
- **What happened:** the UI treated the analyser source as parameter 19 and the 64 spectrum bins as parameters 20-83, but the wrapper makes the bins 19-82 and stores the source as state only.
- **Root cause:** the local `ParameterIndex` enum was copied from the control-only layout and then given an extra parameter that does not exist in the host-facing list.
- **Prevention:** keep one explicit `kAnalyzerSourceValue` slot for the UI's local state mirror, start `kSpectrumFirst` at the real 19 control parameters, and document that the 83 UI values are 19 controls plus 64 outputs.

## spliff: analyser looked dead and PARAMETERS footer overlapped the last slider (2026-11-23)
- **What happened:** the 64-band display appeared empty/hugging the floor in the capture, and the PARAMETERS explanatory sentence was drawn across the last slider track.
- **Root cause:** the screenshot's demo spectrum was authored around -72 dB, which maps to a normalised height near 0.1; the footer was positioned at `panel.y + panel.h - 16`, the same y band as the final track at 632..644.
- **Prevention:** author demo data at a realistic programme level (the replacement curve is centred near -22 dB at 100 Hz) and measure its normalised heights before accepting; keep explanatory text out of the slider geometry and verify track/label bounds from the capture. The fresh 720x680 capture was pixel-checked (bars span the plot, last track is clear) but still needs visual acceptance.

## conductor: new weighted test assumed defaults from a mutated fixture (2026-11-23)
- **What happened:** the late-palette case expected Theme at block 9 but failed, because the same test had already overwritten blocks 1-3 with Null/Theme/Break.
- **Root cause:** a case that asserts default slot positions reused a params array mutated by earlier cases in the same function.
- **Prevention:** start from a fresh `defaults()` fixture whenever a case depends on default slot positions; only mutate a shared fixture when the case explicitly overrides every slot it asserts.

## melevolve: carried-over + vote was drawn white on white (2026-11-23)
- **What happened:** the `+` looked missing on rows carried over from the previous generation, even though all three vote buttons were still registered and clickable.
- **Root cause:** a carried entry arrives with rating `+1`; the UI gave its selected vote a white face to mimic Melogen's outlined style but kept the glyph white too, so the glyph had no contrast.
- **Prevention:** when a selected control changes its fill, set the foreground from the same state (here: vote-colour glyph and 2px border on the hollow button). Verify state colours from captured pixels, not from code inspection alone; the refreshed screenshot was accepted and copied into the catalog.

## fontana: test used a harness-only scratch path (2026-11-22)
- **What happened:** `downspout_fontana_core_tests` aborted at `testWavLoadingAndPeaks` (`assert(loaded.error.empty())`) on a machine where `/tmp/opencode` did not exist. `writeBytes` silently failed to create the WAV, so `loadSampleSource` returned "Could not open WAV file".
- **Root cause:** `scratchPath()` in `fontana_core_tests.cpp` hardcoded `/tmp/opencode`, a path that only exists inside the OpenCode harness, and `writeBytes` did not check that the file opened.
- **Prevention:** use `std::filesystem::temp_directory_path()` (with `create_directories`) for test scratch files, as the NAM and Rift tests do; assert that the ofstream is good before writing. Fixed by deriving the scratch dir from `temp_directory_path()` and adding `assert(stream)` in `writeBytes`.

## melevolve: every UI dropdown silently discarded its selection (2026-11-22)
- **What happened:** the clock source selector stayed on `Internal`, and the other dropdowns (Key, Scale, Time signature, MIDI output channel) were equally inert. Core settings/parameter round-trip tests passed, so only interactive host use exposed it.
- **Root cause:** `MelevolveUI::onMouse()` cleared `openDropdown_` to `-1` before calling `chooseDropdown()`, but `chooseDropdown()` read `openDropdown_` for both its bounds guard and its dropdown lookup. The guard returned immediately, so no parameter was ever committed.
- **Prevention:** pass the open dropdown index explicitly into the commit helper when the same click closes the popup; never clear selection state before the code that consumes it. Verified by driving the standalone UI under Xvfb/XTEST and comparing the right-aligned clock summary before/after each selection (including MIDI channel, which uses a nonzero item offset).

## melevolve: Fave change while host transport was rolling had no audible effect (2026-11-22)
- **What happened:** with Clock source set to Host BBT, starting the transport played row 1; clicking Fave on another row left row 1 playing until the transport was stopped and restarted.
- **Root cause:** `selectFavorite()` only updated `engine.selectedRow`, which `processHost()` reads when arming on a host play edge. Once `hostArmed` was consumed, changing the selection did not retarget the running row.
- **Prevention:** `selectFavorite()` now queues the new row through `engine.pendingPlay` when a non-MIDI clock is running, so the normal play path retargets playback on the next block; MIDI clock is left to Start/Continue. Added `testHostFavoriteSwitchWhileRolling` with a continuous simulated host timeline (a rewinding host position initially masked the bug), and verified the emitted pitches change in a JACK-transport standalone run.

## fontana: pan test fixture was silent after adding a generator (2026-11-22)
- **What happened:** a new hard-left SoundFont test rendered zero audio, so the pan assertion failed before reaching the DSP check.
- **Root cause:** the SF2 test fixture added a `Pan` generator to `igen` but left the instrument bag's end index at 2, so the parser never reached the `sampleId` generator and no voice was allocated.
- **Prevention:** when adding generators to a hand-built SF2 fixture, update the matching bag end index in the same edit. Keep a separate assertion that the unmodified render is audible before asserting pan behaviour.

## fontana: browser directory reset when the editor view was recreated (2026-11-22)
- **What happened:** navigating the built-in browser and then losing focus (host closes/reopens the editor) returned to `$HOME`.
- **Root cause:** `browserDir_` was only a UI member, re-initialised in the constructor; no DPF state key existed for it.
- **Prevention:** persist the directory through a `browser_dir` state key on every navigation and restore it in `stateChanged()`; ignore an empty restored value so a fresh project keeps the constructor default. Don't persist from `refreshBrowser()` itself, or the UI constructor would overwrite the saved directory before DPF sends states on view connect.

## Template

## <plugin/area>: <short description> (YYYY-MM-DD)
- **What happened:** observable failure and how it was found.
- **Root cause:** the specific assumption or missing check.
- **Prevention:** the test, build step or design rule that prevents a repeat.
