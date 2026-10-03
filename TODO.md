# Downspout TODO

* check builds for warning messages. Any that can be resolved in the local codebase should be 

## Refactoring

promotion of the shared OnePoleLowpass/DCBlocker/BiquadFilter duplicates into
  include/downspout/dsp/. That refactor is worth doing but is a separate change and
  CLAUDE.md requires approval before touching shared headers.
  HANDLE WITH CARE

## keyframe display and density scaling

* Density meter saturated by construction (fixed 2026-10-02): `log10(rate)/4.0`
  pegged the bar at 10,000 kf/s against a 24,000 parameter max, and the host
  clamped the output parameter on top, so the meter read stuck and the tick
  strip froze. Fixed by reporting density as the paper's M/N (keyframes per
  input sample), bounded to [0,1] and sample-rate independent; kf/s text was
  dropped because the UI has no sample rate to scale it by.
* Density smoothing was per-block, not per-second (fixed 2026-10-02): a fixed
  0.02 coefficient converged 4x faster at 128-sample blocks than at 512. Now a
  time-based coefficient (tau 0.15 s). The block-size-invariance test covers
  audio only; test 14c pins the status path.
* Leash diagram was static at unity rates because drift is genuinely ~0 there.
  Added a splice halo around the playhead dot so the panel reads live during
  crossfades. If it still reads dead at defaults, the diagram needs a rethink,
  not another lamp.
  
## New Plugins (from DIM)

* [x] Treatment (`/farelo/task/t61f30fb095c3`) - DONE. Mass-air-mass panel
  absorber derived from real cavity / air gap / facing mass / flow resistivity,
  with the impedance-match sweet spot, a seeded Randomise that preserves Amount
  and Bypass, five Drift CC lanes, and a live absorption plot on the panel.
  See `plugins/treatment/README.md` and `plugins/treatment/docs/design.md`.
* [x] combined vocoder and ring mod plugin (`/farelo/task/t12a6e84467ce`) - DONE as
  `voxmod`. Four inputs: 1/2 are the analysed carrier, 3/4 the modulator, which
  is where the task's "modulation on audio channels 3 & 4" ended up. A
  filter-bank vocoder and a polyBLEP-band-limited ring modulator run in parallel
  and blend on Mix, rather than one mode replacing the other. Ring Ratio is
  quantised to 16 musical steps; Formant transposes the modulator side of the
  bank only, which is what moves a vowel. Five Drift CC lanes. Magneto look and
  feel, with the Mix/Ring Freq x-y pad owning those two parameters so they are not
  duplicated as sliders. Core, DPF wrapper, NanoVG UI and 19 deterministic tests
  are complete, plus `docs/design.md`, `README.md`, `profile.ttl` and the catalog
  page. New-plugin checklist closed 2026-10-03: `install.sh` was the last missing
  piece (`-DDOWNSPOUT_BUILD_VOXMOD=ON`); release script, workflow and docs already
  listed it. Screenshot captured and reviewed: the first capture clipped the CC
  drift list at 600px and the hint text was near-invisible, so the window is now
  800x700 and hints use `textDim`. **Still pending:** host validation in REAPER.

## Scales

* [x] add rhumba, samba and township jive (`/farelo/task/td4792ccd13b9`) - DONE as
  *drumgen genres*, not scales. None of the three is a recognised scale (checked
  against the Wikipedia scale list; no web search available to widen that), and
  drumgen already carried rumba/samba clave templates, so `drumgen` was the
  natural home. `GenreId` is append-only: `rumba=14`, `samba=15`,
  `townshipJive=16`, `count=17`, pinned in `testFugueGenrePinsSparsePulse`.
  These are the first genres with a *written* 16th-grid figure rather than a
  per-lane velocity bias, so `applyClaveFigure` clears the lanes it owns and
  re-strikes them. `docs/scales.md` is unchanged.

## Bugs found while doing the above

* [x] **Plugin test suites compiled with `NDEBUG` made every `assert()` dead code**
  (`/farelo/task/te42f69e0eb75`) - DONE 2026-10-03. `CMAKE_BUILD_TYPE=Release`
  adds `-DNDEBUG`, so the suites passed while testing nothing. Added the
  `-UNDEBUG` / `/UNDEBUG` block (as in arpgen and drumgen) to the 32 remaining
  plugin-local suites. After the fix all 61 ctest targets pass with live asserts
  (`__assert_fail` is now linked in), so the triage pass found no further bugs.
  Follow-up, also 2026-10-03 (approved): the root-declared
  `downspout_producer_bus_contract_tests` got `-UNDEBUG` too, and every suite that
  uses `assert()` now includes `include/downspout/test_assert.h`, which `#error`s
  if `NDEBUG` is defined, so a lost `-UNDEBUG` fails to compile instead of passing
  silently. New suites using `assert()` must include it and set `-UNDEBUG`.
* Replace the 36 copy-pasted `-UNDEBUG` + include-dir blocks with one root helper,
  e.g. `downspout_add_core_test(<target> SOURCES ... LIBS ...)`, so a new plugin
  cannot forget either. Worth doing but low urgency, since the `test_assert.h`
  guard already catches the failure. Touches root glue and ~36 plugin CMake files,
  so needs approval and a full test run.
* `cleanupPattern` re-stamped a snare backbeat on every genre *after* the clave
  overlay ran, so clave grooves came out with a rock backbeat.
* `DrumgenUI.cpp` fed `getBundlePath()` (null outside a VST bundle) straight into
  `std::string`, so the standalone jack app crashed on startup with
  `basic_string: construction from null`. This is why drumgen screenshots could
  not be captured.

## Evaluate Manually in Reaper

* helterskelter
* keyframe
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
* tuney-vst
* worms

## Recurring - check periodically

* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
