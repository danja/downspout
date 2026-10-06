# Mistakes log

- 2026-10-05: `scripts/capture-plugin-screenshots.sh` captured the wrong
  window and overwrote the committed `moka.png` with a screenshot of an
  unrelated editor window. `wait_for_window` matches by `_NET_WM_PID` first
  but falls back to `xdotool search --name "$exe"`, and that is a substring
  match: any window whose title happens to contain the plugin slug matches.
  A session titled "Moka impulse removal..." was enough. Root cause: trusting
  a name-based fallback to identify a specific window, in a script whose output
  path is a committed asset. Prevention: after any capture, `identify` the PNG
  and compare it against `DISTRHO_UI_DEFAULT_WIDTH/HEIGHT` before accepting it,
  and open the image. If the fallback ever produces a plausible-looking but
  wrong-sized capture, capture by PID alone.

- 2026-10-05: Two `moka` UI defects that only the rendered screenshot exposed.
  The Voices stepper read "5" while the value and the lit voice slots were both
  4, because the label table was filled with `i + 1` and then indexed by the
  parameter *value*. And every unexcited partial slot drew a full-width dark
  groove, which reads as a loud bar — the exact opposite of "this model does
  not excite this mode", and it buried the difference between a used-but-quiet
  partial and an unused one. Root cause: a stepper's label table is indexed by
  value, not by ordinal, and I filled it in ordinal terms; and I drew the
  "empty" state using the same fill as the "full" state. Prevention: when a
  table is indexed by a parameter value, generate it from the value
  (`"%d", i`) rather than from the position, and clamp the index anyway. When a
  plot has an empty state, give it no mark at all rather than the track mark —
  a track is a positive statement that something occupies the slot.

- 2026-10-01: `treatment`'s first RBJ high shelf was wrong twice over and both
  faults produced filters that looked plausible. Missing the `amp` factor on the
  numerator meant the shelf reached half its requested gain at Nyquist; then
  using `+ (amp - 1) * cos` in the `a` group where the cookbook says `-` turned
  the shelf inside out, so it *boosted* the bottom end by up to +13 dB and cut
  the top. Root cause: transcribing cookbook coefficient blocks from memory
  instead of checking the transfer function, in a class where "attenuate only"
  was the design premise. Prevention: any biquad whose sign is not obvious gets
  a magnitude-response check, and for a filter that must never boost, assert the
  peak of the cascade response across the whole declared parameter space.
  `worstCascadeDb` in the core tests does exactly that and found both faults;
  `transmissionAt` is separately asserted to stay at or below 1.0.

- 2026-10-01: Two `treatment` UI plotting bugs, from a sign error and from
  clamping against the wrong spec table, and both survived review because the
  render "looked fine". `plotYForDb` inverted its fraction, so the absorption
  curve drew as a peak instead of a dip. `flowResistMatchPosition` clamped the
  cavity depth against the *resistivity* minimum (1000) instead of the *cavity*
  minimum (20), which pinned the impedance-match tick to the far left for every
  panel. Root cause: reading the drawing code to check the geometry instead of
  measuring it, and copying a `clampf(x, a.minimum, b.maximum)` without checking
  which spec `a` came from. Prevention: derive a data plot's expected shape
  numerically first and compare it against the rendered image at full
  resolution, cropping the region of interest; in a helper that takes values from
  two spec tables, name the source in the parameter.

- 2026-10-01: The first `treatment` screenshot came out a uniform grey
  760x580 PNG. The window existed and the app log was clean, but the default
  1 s settle captured before the first repaint landed. Prevention: the
  screenshot review step is not skippable — a blank capture is indistinguishable
  from a broken plugin unless someone opens the PNG. A longer
  `DOWNSPOUT_SCREENSHOT_SETTLE_SECONDS` fixed it.

- 2026-09-28: First `ghost` core quantised onset accents to the next 16th slot
  but dropped any ghost whose frame fell outside the current block. At 120 bpm
  a 16th is ~6000 frames while host blocks are 64-512, so nearly every ghost
  with Drag > 0 was silently lost and the core tests caught it (impulse in,
  nothing out). Root cause: thinking in musical time while emitting in block
  time. Prevention: any transport-quantised MIDI emitter needs a schedule
  queue in absolute quarters (due-check per block, drop on seek-past, evaporate
  on stop) — implemented as `ScheduledGhost[8]` with the same bound as the
  per-block emission cap.

- 2026-09-27: First `primefold` pitch-shifter cut used a dual-tap sawtooth
  delay line with a full-cycle raised-cosine crossfade (DAFX textbook shape).
  It measured ~1.85x/2.70x/4.41x instead of 2x/3x/5x on a 220 Hz sine, and the
  error is content-dependent (reference glide of W/2 per half cycle), so it
  cannot be calibrated out. Narrowing the fade to the wrap instants removed
  the drag but exposed the W-sample discontinuity as HF hash (+1200 cents of
  spurious zero crossings). Root cause: assuming the textbook topology without
  measuring shift accuracy first. Prevention: for any new pitch/time DSP,
  write the ratio-accuracy probe (zero-crossing or correlation estimate
  against a sine) before wiring the effect — the shipped design is a
  single-tap SOLA voice (exact ratio between correlation-matched jump-backs,
  verified 440/660/1109 Hz for a 220 Hz input) because that probe failed fast
  on the crossfade variant.

- 2026-09-09: `pkill -f "<pattern>"` hung the persistent shell session because
  the pattern also matched the shell's own command line. Prefer `kill <pid>`
  after resolving PIDs with `ps`, or use patterns that cannot match the
  invoking command.
- 2026-09-09: `pgrep -f "<pattern>"` in `$(...)` has the same self-match trap:
  the pattern string appears in the invoking command line, so pgrep can
  return the caller itself and a following `kill` destroys the session. Use
  the bracket trick (`pgrep -f "foo[.]bar"`) or resolve PIDs via `ps`.
- 2026-09-09: `scripts/capture-plugin-screenshots.sh moka` captured the
  terminal instead of the plugin window because `xdotool search --name moka`
  matched the terminal title. When the operator terminal contains the plugin
  name, capture by explicit window id (`xwininfo -root -tree` filtered by
  PID, then `import -window <id>`).
- 2026-09-14: Commit c48069e ("minor fixes & tweaks") zeroed the gremlin
  defaults for `chaos` (0.38), `crunch` (0.18), and `stutter` (0.20) without
  re-running the plugin core tests. Those three parameters are what drive the
  per-mode timbre differences, so `downspout_gremlin_core_tests` began failing
  its mode-separation assertion (Servo vs Ring measured 0.0285 against a 0.035
  threshold) and stayed broken on `main` until CI surfaced it. Root cause was
  twofold: a parameter-default change was not treated as a behavioural change
  worth testing, and the test itself compared modes with a hand-weighted scalar
  metric (rms/zero-crossings/side/peak/roughness) whose values for every mode
  pair sat in a 0.03-0.04 band -- the threshold was inside the metric's own
  scatter, so any default tweak could flip it. Prevention: run the affected
  plugin's core tests after changing any parameter default, and prefer
  discriminators with real margin. The mode/scene checks now compare coarse
  half-octave log-magnitude spectra with a mean-removed cosine distance, which
  separates every mode pair by ~2x the threshold and is stable across -O0..-O3
  and -ffast-math.

## Measuring a spliced signal

Zero-crossing counts and autocorrelation both misreport the pitch of a spliced
signal: the output is phase-continuous within a segment but phase-discontinuous
across a splice, and both methods read that discontinuity as signal. A Goertzel
magnitude at a candidate frequency is the measurement that survives it.

## Test probes that never advance the input pointer

`processBlock` reads `inputs[0][0..nframes-1]` relative to the pointer it is
given. A probe that passes `in.data()` for every block re-reads the first block
of the buffer forever, which looks exactly like a broken engine: the sparse
buffer fills with nothing but block-boundary keyframes, and the output is a
uniformly sampled version of the input rather than a reconstruction of it.
Several hours went into the core before the probe was suspect. Always offset the
input pointer with the block, and sanity-check a probe against the raw input
before believing what it says about the engine.

## Writing a DSP core in one pass and then compiling it

Three of the mistakes below came from drafting `plugins/plank/src/plank_core.cpp`
in a single write with the effect and render sections left as stubs and a
half-written lambda in `renderDelay` (a `size_placeholder` identifier that never
existed, and a lambda returning a pair into a `void` function). The file did not
compile, and nothing about it had been type-checked. Port a DSP subsystem one
function at a time and build after each one; a large unreviewed write hides real
errors behind the first syntax error.

## Computing a filter coefficient and then not using it

In `renderVoice` the cutoff coefficient was computed, clamped, and then ignored:
`voice.filter.process(input, amplitude, resonance)` passed the envelope as the
filter's coefficient. Plinky conflates the two (`y1 += (... - y1) * vol`), so
copying the call shape carried the conflation across, and the cutoff control did
almost nothing -- peak moved 0.6325 to 0.6313 across the parameter's entire
range. It only showed up because a sweep printed peak per step and the numbers
barely moved. When a control has an obvious expected effect, assert that effect
directly: the plank test now requires a closed filter to be under 0.75x an open
one, and also compares high-frequency content, so a level-only "fix" cannot pass.

## Asserting a wrap condition that is not a wrap condition

The first wavetable test asserted `data[0] == data[size-1]`, reasoning that a
periodic table should meet itself. That is false: sample `size-1` is the last
point *before* returning to sample 0, and the two differ by one ordinary step.
The assertion failed on correct tables and would have been "fixed" by corrupting
the generator. The property that actually matters is continuity: compare the
final-to-first step against the largest step inside the table. That test then
found a real defect -- the pulse tables were built with a two-sample slew that
still produced full-scale (32768) steps -- which the original assertion could
never have surfaced. Prefer asserting a physical property over a guessed one.

## Documenting scale ordinals from the reference table instead of the enum

`docs/scales.md` was updated with plank's ordinals read off the canonical
24-row table, which includes a generic `pentatonic` row at position 16. Plank's
enum does not contain `pentatonic`, so everything from `blues` down was
documented one too high (`blues = 17` when the enum says 16). The plank tests
passed throughout, because they pinned `bebopMinor == 23` and
`ScaleId::count == 24`, both of which happened to be right. Cross-check
generated documentation against the source rather than the reference it was
derived from; a script that parses the enum and diffs it against the doc table
catches this in a second.

## Batch text edits: `if/elif` chain followed by an unconditional append

A Python rewrite of `docs/scales.md` used an `if/elif` chain to choose a
replacement line and then ran `out.append(line)` unconditionally after the chain.
Every branch that matched therefore emitted both its replacement *and* the
original, duplicating table rows, and a separate branch built cells by
`line.rstrip()[:-1]` which stripped the trailing pipe and concatenated two cells
(`| — 0 |`). The result was mangled enough to need `git checkout` and a rewrite.
When appending conditionally in a loop, `continue` in every branch, or build a
list of replacements keyed by exact match and look up rather than branch. Diff
the result before trusting it.

## Three bugs the guitar-string change exposed

Adding `Spread` meant the pitch mapping had to become two-dimensional, which
forced a proper look at the scale tables. Three defects came out of it, and all
three were invisible to the existing tests.

**The scale ladders descended at the top row.** `kScaleIntervals` stored a fixed
octave (12) at index 7, which is only correct for a seven-note scale. For
pentatonic (5 degrees), blues and whole-tone (6) the ladder went
`0,2,4,7,9,12,14,12` — row 7 was *lower* than row 6. The test asserted
`kScaleIntervals[scale][7] == 12`, which those scales happened to satisfy, and
only checked monotonicity on major. Fixed by storing the notes of one octave
plus a per-scale degree count, and wrapping with an octave per repetition. The
test now checks strict ascent across all eight rows for all 24 scales, and the
UI's note readout shares `scaleStepAt()` with the core so it cannot drift.

**`microtune` was applied as whole semitones.** The parameter is in cents with a
default of 8, and the code computed `fine = lround(microtune * 100)` then used
`fine / 100`, which is just `microtune` — so the default patch was 8 semitones
sharp. The test set `microtune` to 0 before checking pitches, which hid it
completely. Fixed by keeping it a `double` all the way to the final rounding, and
the test now asserts 50 cents is a quarter tone and that the default leaves the
pitch alone.

**Plank had no session state at all.** `initState`/`getState`/`setState` were
never implemented, and `DISTRHO_PLUGIN_WANT_STATE` was never set. Two further
mistakes hid it: the constructor was `Plugin(kParameterCount, 0, 2)`, where the
third argument is *stateCount* not outputs, so it declared two states it could
never fill; and `getState()` is gated on a separate macro,
`DISTRHO_PLUGIN_WANT_FULL_STATE`, not on `WANT_STATE`. Chasing it across the repo
found 6 plugins with `WANT_STATE` but no `WANT_FULL_STATE`, which is worse than
having no state: DPF's VST3 wrapper seeds its state map from `initState`'s
`defaultValue` (`DistrhoPluginVST3.cpp:662`) and only refreshes it from the
plugin inside `#if WANT_FULL_STATE` (line 1185), so those plugins write their
**defaults** into every host project and silently discard the user's settings.
24 more plugins have no state at all. `scripts/check-plugin-state.sh` now detects
all three classes.

The common thread is that all three were masked by tests that zeroed the
offending parameter or only checked one scale. When a test needs a parameter to be
inert, assert the *default* is inert rather than forcing it to zero — that is how
the microtune bug survived.

## Plank: the grid ignored its columns and stride was not Plinky's (2026-10-06)

**What happened.** Plank's note layout did not match Plinky. Spread was
implemented in `noteForCell(row, col)` and tested there, but the voice was
pitched through `setDegree()`, which hard-coded column 0, so every string played
the same ladder whatever Spread said. Separately, `Stride` was a flat semitone
offset on all columns instead of Plinky's per-string, scale-snapped stride, and
chromatic was an eight-degree scale so stride and rotate wrapped an octave early.

**Root cause.** The tests asserted on `noteForCell` directly, never on the note a
plucked string actually sounded, so the one call site that dropped the column
was never exercised. The design notes also called stride "a constant push"
without checking `plinky.c`'s `stride()`.

**Prevention.** `testStridePitchReachesTheVoice` plucks each column and compares
the emitted MIDI note with `noteForCell`. When porting from a reference
implementation, read its function for the behaviour before describing it. The
UI's old ruler (row pitches laid out under the column letters) hid the problem
by showing a ladder the sound did not follow; pads now carry their own note
names.

## Plank: resonator engines ran denormals and the host reset the patch (2026-10-06)

**What happened.** With a resonator engine selected, a DAW reported heavy CPU use
and no audio. Offline, per-block cost rose from 0.1 ms to 0.4 ms over thirty
seconds after a single note, and `activate()` / `sampleRateChanged()` called
`init()`, which reset every parameter, including `Engine`, to its default.

**Root cause.** Idle voices kept running their resonators and filter, so
decaying states spent most of their time in the denormal range, and hosts do not
set flush-to-zero. Separately, the wrapper treated "host activated" and "sample
rate changed" as "first-time setup".

**Prevention.** Idle voices are now skipped and their state zeroed, modal state
is flushed per block, and `activate()` / `setSampleRate()` clear only runtime
state. Tests: `testHostActivationKeepsThePatch`, `testIdleResonatorsAreSilentAndCheap`.
Time any new recursive DSP over a long decay, not just the attack, and never
reuse a first-time-setup call for a host lifecycle callback. The "no audio"
symptom was not reproduced offline; the two defects above are what the code
showed, so it still needs confirming in the host.
