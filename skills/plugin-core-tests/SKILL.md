---
name: plugin-core-tests
description: Write and register deterministic core tests for a Downspout plugin without the traps that made past suites pass while testing nothing. Use when adding or changing plugin logic, parameters, defaults or genres.
---

# Plugin core tests

Core tests exercise the portable core only, never the DPF wrapper, so a green run proves nothing about state, lifecycle or UI.

## Setup
- Register with `downspout_add_core_test(<target> SOURCES ... LIBS ...)` from `cmake/DownspoutTests.cmake`. It applies `-UNDEBUG`, the shared include path and `add_test`. Do not copy the long form.
- Suites using `assert()` must `#include "downspout/test_assert.h"`; a lost `-UNDEBUG` becomes a compile error.
- Run: `cmake --build build --target <name>_core_tests -j8 2>&1 | grep -E "error:|Error [0-9]"` then `ctest --test-dir build -R <name> --output-on-failure`.

## Prove a new test actually runs
After adding tests, change the test count or add a temporary `assert(false)` at the end of `main`, confirm it fails, remove it. Say you did. (Runtime changing from 0.01 s to 0.02 s is weak evidence on its own.)

## What to assert
- The observable effect (emitted note, rendered peak), not an intermediate.
- Every control has an audible/visible effect: render with the control at two values and compare. To make a parameter inert, assert its default is inert instead of zeroing it. Zero confounders (Drive, Sub) when isolating one control.
- Re-run the suite after any parameter default change.
- Enum values that are persisted: pin the integers (`assert(int(GenreId::x) == N)`) and assert `count`; the clamp test expects the highest value for out-of-range input.
- Serialization: round trip, old state without the new field (defaults), clamp, reject garbage.
- Host lifecycle: a `testHostActivationKeepsThePatch`-style test (activate/sampleRateChanged must not reset the patch).
- Determinism: same seed gives the same output; different seeds differ.
- Quoted bounds ("worst case over a range") need a test sweeping the whole range.
- Transport-quantised emitters: schedule in absolute quarters; test stopped transport, loop boundary, rewind, tempo change; events outside the block must not be dropped.

## Audio measurement
- Measure before building DSP (probe pitch ratio / filter response first). Use Goertzel or Hann main-lobe energy, not zero crossings or single-bin peaks. Probes must advance the input pointer per block.
- Stability of cascaded boosts: check the impulse response decays, not a level bound.
- Idle voices must not decay into denormals: skip, zero, or flush; time long decays.
- Timing tests ("faster than real time") can fail on a loaded machine: rerun alone before calling it a regression; a CPU figure from a busy machine is an upper bound.

## Gain calibration for generators
Render each patch at full velocity, record peak and RMS, and set per-patch gain so peaks land in a sane band (clatter used 0.2-0.75). Keep the probe, not guesses.
