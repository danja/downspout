# Subsequence: ideas for Downspout

Source: https://subsystem.co/subsequence (read 2026-10-08). Subsequence is a Python,
AGPLv3 generative MIDI sequencer. It outputs MIDI only. These notes come from the
project page, not from its source code. "Already has" claims below are judged from
plugin READMEs and should be checked against the code before work starts.

## What Subsequence does

- Patterns are rebuilt before every cycle with context: current chord, section,
  cycle count and shared data from other patterns.
- Chord graph with weighted transitions and adjustable "key gravity", plus
  automatic voice leading.
- Narmour-style melodic inertia: big leaps tend to reverse, small steps tend to
  continue.
- Euclidean, cellular automata, L-system and Markov generators.
- Shared state (`composition.data`) so patterns cooperate without wiring.
- Sections with section-aware muting and cycle-dependent variation.
- Microtonal output from Scala `.scl` files and N-TET, via per-note pitch bend.
- Seed system for reproducible output.
- Absolute-time pulse scheduling, so no accumulated drift.

## Improvements to existing plugins

1. **Melodic inertia in `melgen` and `counterpointer`.** Bias the next interval:
   after a leap above a threshold favour reversal, after a step favour continuing.
   One "Inertia" control. Deterministic and testable.
2. **Chord graph with key gravity** in `harmonic-atlas` and `cadence`: a slider
   blending free wandering with pull towards the tonic.
3. **Shared voice-leading routine** (minimise total voice movement when choosing
   inversions) for `cadence`, `arpgen`, `harmonic-atlas`. Would live in
   `generative-common`, so it needs approval.
4. **1D cellular-automaton rhythm lanes** (Rule 30/90/110) in `polymeter`,
   `drumgen`, `xoxolo`. `lifeform` already covers 2D Life.
5. **Section and cycle state exposed to other generators** from `conductor`,
   through a CC-lane convention. True cross-instance shared state is not
   practical in VST3.
6. **Per-note microtonal retuning** of any note stream to a Scala scale
   (see "Scala retuner" below). `docs/scales.md` governs scale work.
7. **Cycle-aware variation** ("evolve every N cycles", bounded mutation rate) in
   `bassgen`, `drumgen`, `ground`, with the seed derived from the cycle number so
   renders stay reproducible.

## New plugin candidates

- **Scala retuner** (MIDI effect): load `.scl` (optionally `.kbm`), emit per-note
  pitch bend, configurable bend range.
- **L-system sequencer** (MIDI generator): axiom plus rewrite rules, N
  generations, turtle-style mapping to pitch, duration and rest.
- **Markov melody generator**: order-1/2 with a visible transition matrix. (Done: `plugins/markov/`.)
- **Chord-graph walker**: editable transition graph, gravity control, voice-led
  chord output.

## Not portable

Python as the pattern language, live API and sensor data, and the cross-pattern
data store.

## Priority

1. Inertia control (idea 1).
2. Scala retuner.
3. L-system sequencer.

## Status

| Item | Status |
|---|---|
| Idea 1 inertia | Done in `melgen` and `counterpointer`: `Inertia` control (default 0 = unchanged output), tested, screenshots recaptured. Not yet auditioned in a host |
| Voice-leading | `harmonic-atlas` only (shared helper not made): real voice-leading search behind the Voice-leading control, jump-safe and tested. Default output changes (75% now engages it) |
| Chord graph with key gravity | `harmonic-atlas`: Gravity control (default 0 = unchanged), tested, screenshot recaptured. `cadence` not done. Not yet auditioned in a host |
| 1-D cellular-automaton lanes | `polymeter`: per-lane Automaton rule (0 = Euclidean), appended parameters, tested, screenshot recaptured. Not yet auditioned in a host |
| L-system sequencer | `plugins/sprout/`: core, wrapper, UI, profile, docs, Pages page and root wiring done; core suite passes under ASan/UBSan. Also has optional MIDI input (held or latched notes as the pitch source, Drift CC 1-4, Conductor CC 21-24), all off by default. Not yet auditioned in a host |
| 1-D cellular-automaton lanes in `drumgen` and `xoxolo` | Done 2026-10-09: `drumgen` has an Automaton layer (rule + which lanes it plays on, off by default) that evolves hats, percussion, toms or every lane from the generated pattern each loop; `xoxolo` lets any lane be marked to evolve (rule and generation length are parameters). Both are deterministic from the playback position, leave the stored pattern untouched, and are tested against a reference automaton and under ASan/UBSan. The automaton code is a plugin-local copy in each of `polymeter`, `drumgen` and `xoxolo` (a shared helper in `generative-common` would remove the duplication but needs approval). Not yet auditioned in a host |
| Markov melody generator | Done 2026-10-09: `plugins/markov/`. A random walk over pitch classes through a visible, editable 12 x 12 matrix, eight styles, learning from a melodic line on the MIDI input (first and second order), scale masking, Chaos, phrase-restarting repeatable walk, Conductor CCs, Magneto-look panel. Core suite passes under ASan/UBSan. Not yet auditioned in a host |
| Shared automaton arithmetic | Done 2026-10-09: `include/downspout/cellular_automaton.hpp` (with its own suite, `tests/cellular_automaton_tests.cpp`) is used by `polymeter`, `drumgen` and `xoxolo` |
| Conductor awareness | `lifeform`, `polymeter` and `xoxolo` now listen to Conductor (CC 21-24 on a chosen channel; off by default). Not yet auditioned in a host |
| Idea 7 cycle-aware variation | Checked 2026-10-09: `bassgen`, `drumgen` and `ground` already have variation and mutation loops with progress tracking (Vary, auto-mutation), so this is largely covered. Whether the seed is derived from the cycle number, so offline renders match, has not been verified |
| Scala retuner | `plugins/retune/`: core, VST3 wrapper, UI, profile, docs, Pages page and root wiring done; core suite passes. Per-channel pitch bend in existing instruments is tracked in TODO.md |
