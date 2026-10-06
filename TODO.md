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

## Release bundle lists have drifted (found 2026-10-06, while adding plank)

Four separate hand-maintained lists have to agree about which bundles exist, and
nothing checked that they did. Verified counts as of today: 63 `DOWNSPOUT_BUILD_*`
options in the root `CMakeLists.txt`, 61 plugins with a CMake `install(DIRECTORY
... .vst3)` rule, 61 bundles in `scripts/package-release.sh`'s `required_bundles`,
61 in `scripts/package-built-bundles.sh`, and 61 in the `.github/workflows/release.yml`
notes. All four now agree. What the drift was:

* `scripts/package-release.sh` `required_bundles` was missing 7 bundles that have
  CMake install rules: `bassops`, `bubbles`, `damiano`, `flues_synth_driver`,
  `gater`, `midiscribe`, `sidecar`. This one was a **hard gate** — lines 162-167
  abort packaging if a listed bundle is absent from the staged install, so the
  failure mode was a release build that dies late. (`sidecar` belongs only in the
  `sidecar_build == ON` variant, which was already correct.)
* `scripts/package-built-bundles.sh` was missing 9: the same set less `sidecar`
  plus `keyframe`, `treatment`, `voxmod`.
* `install.sh` omitted 4 build options that exist: `DOWNSPOUT_BUILD_AI_COORDINATOR`,
  `DOWNSPOUT_BUILD_BASSOPS`, `DOWNSPOUT_BUILD_BUBBLES`, `DOWNSPOUT_BUILD_CHIPPER`,
  and listed `DOWNSPOUT_BUILD_KEYFRAME` twice. Harmless only because every option
  defaults to `ON`, so the array is documentation rather than behaviour — which
  is exactly why it drifted.
* `.github/workflows/release.yml` was missing the same 7 as `package-release.sh`.

**All of the above fixed 2026-10-06.** All 10 bundles were built and confirmed to
produce a valid `.so` *before* being added to the hard gate, since adding a name
to `required_bundles` that does not actually build would turn a documentation fix
into a broken release.

* [x] **`scripts/check-bundle-lists.sh` added.** Compares the authoritative set
  derived from `dpf_add_plugin()` + `install(DIRECTORY ...)` across
  `plugins/*/CMakeLists.txt` against all four lists plus `install.sh`. It also
  checks each plugin's `dpf_add_plugin` name matches its install rule, and
  reports duplicated `install.sh` options. `--list` prints the authoritative set.
  It is **variant-aware**: `package-release.sh` and `package-built-bundles.sh`
  each have a base list and a `sidecar_build=ON` variant, checked separately
  rather than merged. That distinction matters — the first version merged them
  and so could not see a bundle removed from the base list while still present in
  the sidecar one, which a deliberate negative test exposed. `sidecar.vst3` is
  the one bundle legitimately absent from the base list, declared in
  `CONDITIONAL_FROM_BASE` with a comment explaining that the conditionality lives
  in the shell scripts and not in any CMakeLists (sidecar's install rule sits
  behind the same `DOWNSPOUT_ENABLE_DPF` guard as every other plugin). The union
  of all variants is checked against the authoritative set, so that exception
  list cannot go stale unnoticed.
* [x] **Wired into CI** as a new fast `bundle-lists` job ahead of the 30-minute
  package builds, plus a `bash -n` pass over `install.sh` and `scripts/*.sh`. The
  point is timing: this drift was previously caught only by
  `package-release.sh`, i.e. after configure, build, ctest and staging.
* [x] Negative-tested. All five regression classes are detected: a bundle dropped
  from the base variant only, dropped from every variant, a typo'd bundle name, an
  `install.sh` option removed, and a `CONDITIONAL_FROM_BASE` entry appearing in no
  variant. Passes cleanly on the repaired tree.

## Minor cleanups (found 2026-10-06)

* [x] `plugins/damiano/tests/damiano_core_tests.cpp` included `<cassert>` but made
  no `assert()` calls; it uses its own `CHECK`/`gFailed` harness like syrinx,
  midiscribe and flues-synth-driver. DONE 2026-10-06: include removed, with a
  comment recording why the suite does not need `-UNDEBUG`. Tests still pass.
* **BUG: `ground` and `melgen` report the wrong scale name for three selectable
  scales.** Found by the `-Wall -Wextra` sweep below. Both plugins' `ScaleId`
  includes `ionian`, `neapolitanMajor` and `neapolitanMinor`, and both expose the
  whole enum as the Scale parameter range (`ground/src/dpf/GroundPlugin.cpp:292`
  uses `ScaleId::count - 1`). But the `scaleName()` switches handle `count`
  explicitly and have **no `default:`**, so those three fall out of the switch
  to `return "minor"`. `ground`'s `scaleName` is what emits `"scale"` in the AI
  coordinator hand-off (`ground_ai_state.cpp:27`, used at line 131), so
  selecting Neapolitan Minor on ground tells the coordinator it is playing
  minor. Affected: `plugins/ground/src/ground_ai_state.cpp:27`,
  `plugins/ground/src/ground_pattern.cpp:115`,
  `plugins/melgen/src/melgen_pattern.cpp:154`. Fix by adding the three cases,
  then check whether any other `ScaleId` switch in the repo has the same shape.
  `docs/scales.md` already warns that appending a scale silently reinterprets
  saved state; this is the other half of that hazard, where a newly appended
  scale quietly reports as `minor` instead of failing to build.
* Not bugs, but noted while triaging the sweep:
  * `cadence` (2) and `drumgen` (1) `memset`/`memcpy` structs that are
    standard-layout and trivially copyable, so it is safe in practice even
    though `-Wclass-memaccess` fires on their default member initialisers. One
    semantic wrinkle: `cadence_clear_progression` zeroes `ChordSlot`, whose
    `velocity` defaults to `96`, so a cleared slot has velocity `0` rather than
    the constructed default. Consistent within the plugin, but worth deciding.
  * `campione/src/campione_sample_loader.cpp:200` ignores the `[[nodiscard]]`
    `parseSmplChunk` result. It only returns false for a truncated `smpl` chunk,
    where the fallback is "no smpl data", so it is benign — but a truncated
    chunk silently loses loop points on a sampler. The same function also reads
    only the first of `numSampleLoops` loop records, despite the comment
    describing the record layout.

## -Wall -Wextra sweep over the plugin cores (found 2026-10-06)

Standing task at the top of this file, now done with the flagset named rather
than "whatever the default build happens to emit". Every `plugins/*/src/*.cpp`
compiled standalone with `-std=c++17 -Wall -Wextra -fsyntax-only`: **31 warnings
across 11 plugins**. Plank is clean (it was written against the flagset, and two
of its own warnings were removed rather than logged).

| plugin | n | classes |
|---|---|---|
| ground | 14 | `-Wswitch` unhandled `ScaleId` (the bug above), plus `PhraseRoleId::count` |
| melgen | 3 | `-Wswitch` unhandled `ScaleId` (the bug above) |
| gater | 3 | unused parameters in `gater_engine.cpp:6-9` — looks like a stubbed API |
| drumgen | 3 | unused parameters, one `-Wclass-memaccess` |
| campione | 2 | unused parameter, ignored `[[nodiscard]]` |
| cadence | 2 | `-Wclass-memaccess` (benign, see above) |
| drumkit | 2 | unused `vel` in `CrashVoice.hpp:85`, `ClapVoice.hpp:89` |
| bassgen | 1 | unused `beatIndex` in `bassgen_pattern.cpp:1406` |
| sidecar | 1 | unused `intValue` in `sidecar_serialization.cpp:120` |

Most are cosmetic (unused locals and parameters). The `-Wswitch` class is the
only one that found a real defect, and it is worth taking seriously for the
reason above: an incomplete enum switch is exactly how a newly appended scale
becomes a silent behaviour change instead of a compile error.

Suggested follow-up: add `-Wall -Wextra` to `downspout-project-options` and clear
the resulting noise plugin by plugin so this cannot regress. That touches the
shared options target, so it needs approval and a full 62-suite run.

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
* plank
* tuney-vst
* worms

## Recurring - check periodically

* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
