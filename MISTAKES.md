# Mistakes log

Grouped by the pattern that keeps recurring. Each entry says what to do, with the
test, script or incident that proves it. One-off slips with no lesson are not
kept here.

## 1. A test that does not exercise the real path (6 incidents)

The most common failure. The suite passes, the product is wrong.

- **Plank column ignored (2026-10-06).** Spread was tested through `noteForCell`,
  but the voice was pitched by `setDegree()`, which hard-coded column 0, so every
  string played the same ladder. Fixed by `testStridePitchReachesTheVoice`, which
  plucks each column and compares the emitted note.
- **Plank scale tables (2026-10-06).** Tests checked that ladders ascend, never that
  the notes were right, so both Neapolitan scales and the five eight-note scales
  were wrong. `testEveryPadStaysInTheChosenScale` holds independent pitch-class
  sets for all 24 scales.
- **Plank microtune and ladders.** A test zeroed `microtune` and so hid that the
  default was 8 semitones sharp; the ladder test pinned `[7] == 12`, which
  pentatonic and blues happened to satisfy while descending at the top row.
- **Gremlin defaults (2026-09-14).** A default change was not treated as behaviour,
  and the mode-separation threshold sat inside the metric's own scatter. Modes are
  now compared by half-octave log-spectra with a mean-removed cosine distance,
  about 2x margin.
- **Ghost (2026-09-28).** Onsets were quantised to the next 16th but dropped when
  outside the current block (a 16th is ~6000 frames, blocks are 64-512). Any
  transport-quantised emitter needs a schedule queue in absolute quarters.
- **Probes that never advance the input pointer.** `processBlock` reads
  `inputs[0][0..n-1]` relative to its argument; passing `in.data()` every block
  re-reads block 0 forever and looks exactly like a broken engine. Offset the
  pointer, and sanity-check a probe against the raw input first.

**Rule.** Assert the *observable effect*, not an intermediate. When a test needs a
parameter inert, assert the default is inert instead of forcing it to zero. When a
control has an obvious effect, test that it has it (a plank cutoff sweep moved the
peak 0.6325 to 0.6313 because the coefficient was computed and never used). Run the
affected plugin's tests after changing any parameter default.

## 2. Assuming a reference's behaviour instead of measuring it (5 incidents)

- **Treatment high shelf (2026-10-01).** Cookbook coefficients transcribed from
  memory: a missing `amp` factor, then a flipped sign that made a never-boost shelf
  boost by +13 dB. `worstCascadeDb` now checks the peak response across the whole
  parameter space.
- **Primefold (2026-09-27).** The textbook dual-tap crossfade shifted 1.85x not 2x,
  content-dependently. Write the ratio-accuracy probe before wiring any
  pitch or time DSP; the shipped single-tap SOLA design came from that probe failing
  fast.
- **Plank stride (2026-10-06).** Described as "a constant push" without reading
  `plinky.c`'s `stride()`. Read the function before describing it.
- **Plank scale tables and ordinals.** Written without diffing against the existing
  tables in other plugins, which already disagreed with it; and `docs/scales.md`
  ordinals were read off the reference table, not the enum (`blues` one too high).
  When a table exists elsewhere in the repo, diff against it first, and generate
  docs from the source rather than from the thing the source was derived from.
- **Measuring a spliced signal.** Zero-crossings and autocorrelation misread a
  splice's phase discontinuity as signal. Use a Goertzel magnitude at the
  candidate frequency.

**Rule.** Measure first (response, ratio, tuning), then build.

## 3. Host lifecycle and persisted state (3 incidents)

- **Session state (2026-10-06).** `WANT_STATE` without `WANT_FULL_STATE` makes the
  host save the *defaults* from `initState()` into every project, silently. 6
  plugins had it (now fixed) and 24 have no state at all. Also: `Plugin`'s third
  constructor argument is *stateCount*. `scripts/check-plugin-state.sh` detects all
  three classes. The core tests never touch the wrapper, so a green build proves
  nothing: change a setting, save, reopen.
- **Activate / sample rate reset the patch (2026-10-06).** `activate()` and
  `sampleRateChanged()` called `init()`, resetting every parameter (including
  plank's `Engine`) whenever a host started playback. The Launchpad plugins
  (lifeform, luma, paunchlad) were copies of the same shape, and gremlin,
  gremlin-driver and flues-synth-driver still reset on a rate change. Lifecycle
  callbacks must clear runtime state only; keep `init()` for first-time setup, and
  give each processor `activate()` / `setSampleRate()` that leave the patch alone.
  Old tests that used `activate()` to mean "back to defaults" must call `init()`.
  `testHostActivationKeepsThePatch` in plank, lifeform, luma and paunchlad.
- **Denormals (2026-10-06).** Idle voices kept running recursive filters, decaying
  through the denormal range (4x CPU after one note). Skip idle voices, zero their
  state, flush tiny values per block, and time any new recursive DSP over a long
  decay, not just the attack.

## 4. Screenshots and UI checked by looking, not by trusting (5 incidents)

- **Wrong window captured (2026-09-09, 2026-10-05).** `xdotool search --name` is a
  substring match, so a terminal or an editor titled with the plugin name was
  captured, once overwriting the committed `moka.png`. Capture by PID or window id,
  then `identify` the PNG against `DISTRHO_UI_DEFAULT_WIDTH/HEIGHT` and open it.
- **Blank capture (2026-10-01).** A uniform grey PNG: the 1 s settle fired before the
  first repaint. Raise `DOWNSPOUT_SCREENSHOT_SETTLE_SECONDS`. A blank capture is
  indistinguishable from a broken plugin unless someone opens it.
- **Moka UI (2026-10-05).** A stepper's label table was filled by ordinal but
  indexed by value, so it read 5 for 4; and an "empty" slot used the "full" mark.
  Generate label tables from the value and clamp the index; give an empty state no
  mark at all.
- **Treatment plots (2026-10-01).** An inverted fraction drew a dip as a peak, and a
  clamp took its minimum from the wrong spec table. Derive the expected shape
  numerically, then compare it with the image at full resolution.

**Rule.** The screenshot review step is not skippable. Never accept a capture
unchecked.

## 5. Process hazards

- **`pkill -f` / `pgrep -f` self-match (2026-09-09).** The pattern appears in the
  invoking command line, so the call finds (and a following `kill` destroys) the
  shell. Use `ps` to resolve PIDs, or the bracket trick (`foo[.]bar`).
- **Batch edits.** A Python rewrite appended unconditionally after an
  `if/elif` chain, duplicating matched lines. `continue` in every branch, or look
  up replacements by exact match, and diff the result before trusting it.
- **Large unreviewed writes.** Drafting a whole DSP core in one pass with stubs
  hides real errors behind the first syntax error. Port a subsystem a function at
  a time and build after each.
- **Blind first-match replacement (2026-10-06).** Removing "the unused line" by
  string match removed the first of four identical lines, in a function that used it.
  The compiler caught it. Anchor on line numbers or surrounding context when a
  line is not unique.
- **Screenshot script captured the real desktop (2026-10-07).** `scripts/capture-plugin-screenshots.sh`
  only starts Xvfb when `DISPLAY` is empty. Run from an agent session that had `DISPLAY`
  set, it screenshotted the user's actual screen (browser tabs, bookmarks) into
  `docs/pages/assets/plugins/pratt.png`. Caught at the mandatory full-resolution review,
  restored from the earlier Xvfb capture before anything was committed. Prevention: always
  run it as `env -u DISPLAY scripts/capture-plugin-screenshots.sh <plugin>` (or under
  `xvfb-run`) and open every capture before keeping it. The script should be changed to
  always use Xvfb; that is shared glue, so it needs approval.
- **Cross-repo parity script, wrong field order (2026-10-07).** `Note(...)` was built
  positionally with `program` and `channel` swapped, which showed as a bogus 10-18 dB
  mismatch for every sustained voice. Read the dataclass before constructing it, and
  distrust a large mismatch on only some cases.
- **Build errors hidden by my own grep, stale test binary reported as passing (2026-10-08).**
  After adding a pitch-bend test to `canticle`, I filtered build output with `grep " error "`
  (spaces around the word). GCC prints `error:`, so the compile failure
  (`StereoFrame` not in scope in the test) matched nothing, `ctest` then ran the previous test
  binary, and I reported the new test as passing. The user hit the error building canticle.
  Prevention: filter with `grep -E "error:|Error [0-9]"` and also check the build's exit status;
  after adding a test, confirm it runs (a deliberately failing assertion, or the test count or
  runtime changing) before saying it passes. The test is now fixed and the whole tree rebuilt
  with the correct filter: 68 of 68 pass.
- **Wrong stability bound documented and tested for Pratt (2026-10-08).** `design.md`, the Pages
  page and `docs/algorithms.md` said every pole has `Re(t - 2) < -1.02` for n up to 8192. The
  test only swept every 7th index above 2000 and asserted `< -1.0` with the comment "about
  -1.026", so it missed n = 6173, whose worst root is at -0.9977. Found while probing larger
  bases. The filter is still strictly stable; only the margin was overstated. Prevention: when a
  doc quotes a "worst case over a range", the test must cover the whole range (a full sweep was
  cheap, under half a second) and the assertion should be the bound that is documented.
- **Screenshot capture grabbed the real desktop again (2026-10-08).** Re-capturing `pratt` I ran
  `scripts/capture-plugin-screenshots.sh pratt` with `DISPLAY=:0` set, ignoring the entry above.
  The capture was the user's browser, and I had already copied it over
  `docs/pages/assets/plugins/pratt.png` and `plugins/pratt/docs/screenshot.png` (the latter
  also lost its original Synth + Filter view). Noticed on the very next full-resolution look;
  both files were overwritten with a correct capture before finishing. Prevention: run it as
  `env -u DISPLAY scripts/capture-plugin-screenshots.sh <plugin>`, capture into the scratchpad
  first, open the image, and only then copy it into the repo. The script still needs to force
  Xvfb itself (shared glue, needs approval).
