# Downspout TODO

Open items only. Finished work is recorded in the plugin docs and `MISTAKES.md`.

* check builds for warning messages. Any that can be resolved in the local codebase should be
  (the `-Wall -Wextra` sweep below is the current state of this).

## Needs your approval first (shared or root code)

* **Promote the shared `OnePoleLowpass` / `DCBlocker` / `BiquadFilter` duplicates into
  `include/downspout/dsp/`.** Worth doing but a separate change, and CLAUDE.md requires
  approval before touching shared headers. HANDLE WITH CARE.
* **Replace the 36 copy-pasted `-UNDEBUG` + include-dir blocks with one root helper**, e.g.
  `downspout_add_core_test(<target> SOURCES ... LIBS ...)`, so a new plugin cannot forget
  either. Low urgency, since `include/downspout/test_assert.h` already makes a lost
  `-UNDEBUG` a compile error. Touches root glue and ~36 plugin CMake files, so it needs
  approval and a full test run.
* **Add `-Wall -Wextra` to `downspout-project-options`** and clear the remaining noise
  plugin by plugin so it cannot regress. Needs approval and a full 62-suite run.
* **Make `scripts/check-plugin-state.sh` a blocking CI step** in `.github/workflows/ci.yml`.
  It already exits 1 when a plugin declares `WANT_STATE` without `WANT_FULL_STATE`, and the
  6 offenders are fixed, so it can gate now. Shared CI glue, so approval first.

## Session state (found 2026-10-06)

`scripts/check-plugin-state.sh` reports **40 correct / 0 broken / 21 no state**. The six
plugins that silently wrote their defaults into every project (chipper, damiano, ghost,
helterskelter, skream, spliff) now have `WANT_FULL_STATE` and a `getState()`.

- [ ] **Verify those six in a host.** The failure was silent, so a green build proves
      nothing and the core tests never touch the wrapper. Change a setting, save,
      reopen, confirm it stuck.
- [x] **lifeform, luma and paunchlad now have session state** (2026-10-06), following
      Plank's pattern: one versioned, symbol-keyed text state, bad input rejected without
      touching the patch, triggers never stored. Lifeform and luma also save the live
      64-cell pattern as a bitmap (restoring lifeform's `seed` deliberately does not
      reseed over it); paunchlad's pad cells are momentary so only its nine settings
      are stored. Each got round-trip, rejection, trigger and host-activation tests.
      **Verify in a host**, as with the six above.
- [ ] **21 plugins still have no session state at all**, so everything reverts to defaults
      when a project is reopened: arpgen, basilico, canticle, conductor, drift, drumkit,
      floozy, flues-synth-driver, gater, gremlin-driver, gremlin, guardian,
      harmonic-atlas, m-mix, mixgen, moka, oracle, orbit, polymeter, resonance-garden,
      syrinx. Each needs a serialisation format, which is real work. The lifeform, luma
      and paunchlad state files (`*_serialization.cpp`) are small templates to copy.
- [ ] **`gremlin`, `gremlin-driver` and `flues-synth-driver` reset every parameter when the
      host changes sample rate**: `sampleRateChanged()` calls `processor_.init()`, which
      restores defaults. The same bug fixed in plank, lifeform, luma and paunchlad. Give
      them a `setSampleRate()` that keeps the patch (fold it into adding their state).
- [ ] `gater` declares the state macros but has `stateCount` 0 (inert and misleading); give
      it state or remove the macros.
- [ ] Re-run the `-Wall -Wextra` sweep after any state change: the wrapper callbacks are
      what the core tests never touch.

## `-Wall -Wextra` sweep: what is left

Standalone `g++ -std=c++20 -Wall -Wextra -fsyntax-only` over `plugins/*/src/*.cpp` (add
`-Iplugins/generative-common/include` or conductor, drift, ghost, guardian, harmonic-atlas
and others will not compile). No `-Wswitch` warnings remain. What was left on purpose:

* `cadence` (2) and `drumgen` (1): `-Wclass-memaccess` on `memset` of structs with default
  member initialisers. Safe in practice. One wrinkle to decide: `cadence_clear_progression`
  zeroes `ChordSlot`, whose `velocity` defaults to 96, so a cleared slot has velocity 0
  rather than the constructed default.
* `mnemosyne`, `mosaic`: `-Wmisleading-indentation`. The cores are dense one-line
  statements; reformatting is churn, not a fix.
* `campione/src/campione_sample_loader.cpp:200` ignores the `[[nodiscard]]` result of
  `parseSmplChunk`. It only returns false for a truncated `smpl` chunk, so it is benign, but
  a truncated chunk silently loses loop points on a sampler. The same function also reads
  only the first of `numSampleLoops` loop records, despite the comment describing the
  record layout. A behaviour question, not a warning to silence.

## Plank

- [ ] Host validation in REAPER, especially the resonator engines: CPU load, audio out, and
      the patch surviving playback start and a sample-rate change (the fixes are covered by
      offline tests only, and the original "no audio" symptom was never reproduced offline).
- [ ] Plinky's other scales (Hirajoshi, Insen, Iwato, Minyo, major and minor triads, fifths,
      Romanian) are not in Plank. Adding them means appending to `docs/scales.md`. The
      microtonal ones (Harmonics, Hexany, Just) also need a cents-based table, which the
      current integer ladder cannot express.
- [ ] The root `README.md`, `docs/plan.md` and `docs/summary.md` still call Plank an
      "eight-string synthesizer". Shared docs; wording agreed, awaiting approval to change.

## Other

* `voxmod`: host validation in REAPER still pending.
* `keyframe`: the leash diagram is static at unity rates because drift is genuinely ~0 there;
  a splice halo was added so it reads live during crossfades. If it still reads dead at
  defaults, the diagram needs a rethink, not another lamp.

## Pratt (new plugin, from `~/github/pratt-synth`)

Fully wired (root CMake option, install/release scripts, workflow, docs, product page,
screenshot script entry and asset). 4 core suites pass, threading suite clean under TSan.
Remaining:

* **Verify in a host (REAPER):** load, play notes, filter an audio track, save/reopen state.
  Nothing has been listened to. Also confirm the host accepts an instrument with audio inputs.
* Listen to drum levels (normalised per hit, consistent at ~0.15); check voice-stealing clicks.
* GM Program mode warms only the last requested voice; other voices build on first note
  (~1.9 ms). Warm several if this is audible.
* `scripts/capture-plugin-screenshots.sh` captures the real desktop if `DISPLAY` is set
  (see `MISTAKES.md`). Make it always use Xvfb. Shared script, needs approval.
* Catalogue screenshot shows Synth mode (filter dimmed); consider a Synth + Filter capture.

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
