# Pratt: design notes

Source: [pratt-synth](https://github.com/githubuser1983/pratt-synth) (offline Python MIDI-to-MP3 renderer,
`pratt_midi_synth.py`). This plugin is a real-time port. Status: portable core,
engine (synth, filter, both), DPF wrapper, state and custom UI are written and build
as VST3 and JACK standalone. Wired into the root build, install and release scripts,
catalogue and docs (2026-10-07). Not yet listened to in a host.

## DPF wrapper

- `src/dpf/PrattPlugin.cpp`: 2 in / 2 out, one MIDI input, `IS_SYNTH`, `Instrument|Synth`.
  Splits each block at MIDI event frames and calls `Engine::process` per span.
- 13 automatable parameters plus 2 read-only status outputs (effective index `n`,
  sounding voices) in `include/pratt_params.hpp`, which also holds the settings ->
  engine mapping.
- State: one `settings` key, `version=1` followed by `symbol=value` lines
  (`pratt_serialization.cpp`). Strict parse: bad version, unknown or duplicate keys,
  missing keys and non-finite numbers are rejected without touching the current
  settings; a good document is clamped. Voices and filter memory are never stored.
- Warm-up (`Warmer` in `PrattPlugin.cpp`). It exists so the first chord does not build
  tables on the audio thread, and it is built so it can never stall the host:
  - the worker starts in `activate()` (not the constructor, so host scans cost nothing)
    and is joined in `deactivate()`, on sample-rate change and on destruction;
  - work is cut into steps (one pitch's tables, 64 root indices, one drum filter);
    between steps it checks for shutdown or a newer request and sleeps 1 ms, so a
    join takes about one step and the worker never hogs a core;
  - the audio thread only `try_lock`s a mutex the worker holds for a few instructions,
    never waits and never signals a condition variable; a request that finds the lock
    busy is retried on the next block; the worker polls every 20 ms when idle;
  - a voice request preempts background work (roots, drums); requests queue newest
    first (up to 8 voices, so Program Changes on several channels each get warmed); a
    newer request abandons the build in progress, which goes to the back of the queue and
    resumes from its cached tables; pitches are built middle-outwards (C4 first), range
    12-108;
  - stale tables from a previous tuning are dropped on the worker (`trimTables`, freed
    outside the lock); the cache is capped at 4096 tables (about 64 MB) and past the cap
    new tables are built but not cached, so the audio thread never frees a big cache;
  - heavy work (table synthesis, root finding) runs outside every lock;
  - no warm-up in Filter mode; in GM Program mode each Program Change queues its voice
    (the last 8 are kept), anything older builds on first use (about 1.9 ms per note).
  `tests/pratt_threading_tests.cpp` replays this pattern and is clean under
  ThreadSanitizer (see the file header for the command).
- Parameters are written from the host thread and read on the audio thread without
  locks (same pattern as magneto).
- `src/dpf/PrattUI.cpp`: magneto look and feel. Mode buttons with a signal-flow line,
  a VOICE panel (stepper, sliders, drums toggle, harmonic bars of middle C for the
  current base/brightness/rolloff) and a FILTER panel (indexes, `n = A x B`, order,
  cutoff, mix, magnitude response with the cutoff marker, pole plot). The inactive
  section is dimmed but stays editable.

## Verification against the Python renderer

`tools/pratt_parity.py` (with `tools/pratt_dump.cpp`) renders a held note through
`melodic_voice()` and through the C++ engine, then compares harmonics 1-12 over a
steady window. Organ, piano (early and late), strings, flute and reed all agree
within 0.12 dB. Envelopes, vibrato and the bright/dark crossfade are therefore
faithful; what differs is by design (see above). The tool needs numpy, scipy and the
`pratt-synth` checkout, so it is not part of the CMake test run.

Timing (-O2, one core): first note of an unwarmed piano voice builds two tables in
about 1.9 ms on the audio thread, a warm note-on takes about 1 us, a filter
retarget about 7 us. A cold chord of several notes could exceed a 256-frame buffer,
which is what the `Warmer` is for.

## Visual review (screenshot at `docs/screenshot.png`)

Reviewed at full resolution in all three modes: purpose, signal flow, grouping and
values read without the source; text is not clipped. Two defects found and fixed:
a visible seam in the dimmed panel header, and unlabeled dB gridlines. Remaining
nits: the response plot floors at -60 dB, and the pole plot has no axis labels.
`docs/pages/assets/plugins/pratt.png` is produced by `scripts/capture-plugin-screenshots.sh`
(default state, Synth mode); `docs/screenshot.png` is a Synth + Filter capture.

## Engine (`pratt_engine.hpp`)

`Engine::process(inL, inR, outL, outR, frames)` plus MIDI methods (`noteOn`,
`noteOff`, `controlChange`, `pitchBend`, `programChange`, `handleMidi`). The wrapper
splits blocks at MIDI events. `EngineParams` carries everything the UI exposes.

| Mode | Signal |
|---|---|
| Synth | MIDI -> voices -> room -> HP/master/soft clip |
| Filter | audio in -> Pratt filter (dry/wet mix) |
| Both | (audio in + synth) -> Pratt filter |

Filter index is `n = indexA * indexB` (clamped to 8192), so two controls cascade
`H_m H_n = H_mn`. Cutoff is `w0/2pi`, clamped to 0.1 fs. Parameter changes
crossfade old and new cascades over 20 ms.

Ported from Python: 11 presets and GM program mapping (verified against all 128
programs), velocity buckets, bright/dark crossfade, vibrato, CC7/10/11/64,
pitch bend, 14-tap room, 22 Hz high-pass, percussion on channel 10 (kick plus
noise through `H_{pitch+1}` at 6 kHz).

Intentional differences:

- gate-based envelopes (attack not shortened for short notes; release runs to -80 dB
  where Python cut at -20 dB and faded 12 ms);
- fixed master gain `tanh(0.5 x)` instead of per-file peak normalisation;
- 64 voices with stealing (quietest releasing voice, else oldest; steals can click);
- `bendRange` is a parameter, RPN is not parsed;
- drums are normalised per pitch by simulating a hit and dividing by its peak (Python
  normalises each hit), so all drums land at the same level (~0.15 at velocity 110 after
  master gain, channel volume and pan); the noise source is uniform, not Gaussian. Levels
  are consistent but still **unlistened**;
- extra parameters not in Python: preset override, base override, xi scale, roll offset.

Real-time caveat: table and cascade construction (`noteOn`, `setParams`) allocate and
take a mutex. Call `Engine::warmCaches()` and `preload(preset)` from a worker thread
at start-up and when the preset changes. The wrapper must decide how to schedule this.

## The algorithm

- `f_1 = 1`, `f_2 = x`, `f_p = 1 + f_{p-1}` (odd prime p), `f_n = prod f_p^v`.
  `f_n(2) = n`, `f_mn = f_m f_n`.
- `H_n(s) = n / f_n(2 + s/w0)`: stable all-pole low-pass, `H_n(0) = 1`,
  `H_mn = H_m H_n`. Poles are `w0 (t_j - 2)` for roots `t_j` of `f_n`.
- Synth voice: `n = (midi_pitch + 1) * base`; partial k of the note has weight
  `k^-roll * H_n(i * xi * k)` (magnitude and phase). Summed into a single-cycle
  table.
- Timbre chain: `R(k) = H_n(i xi k) * H_t(i xi k)` with `t` the Timbre Index (1..8192, default 1).
  `H_mn = H_m H_n`, so the chain is a cascade that is independent of the note and not limited by
  `kMaxIndex` as a combined index would be. At `t = 1` the chain is skipped; the golden wavetable
  tests (to 1e-5) still pass, so the default sound is unchanged. Cost is nil per sample (the synth reads
  tables, it runs no biquads) and unmeasurable per table (about 0.8 ms either way). Only Filter
  mode runs biquads, one per pole pair, as before. The Timbre value is part of the table cache key
  and `trimTables` drops tables for stale values. State key `timbre` is optional on load so
  projects saved before it existed still open. Added after the first release, appended as input
  parameter 13; the two output status parameters moved to 14 and 15.

## Measured facts (all n <= 8192, verified in `pratt_core_tests`)

| Question | Answer |
|---|---|
| Polynomial degree | <= 13 |
| Largest integer coefficient | 70 (int64 is exact with huge margin) |
| Stability | every root has `Re(t - 2) < -0.997` for n <= 8192 (worst at n = 6173), so all poles are strictly stable |
| Root conditioning | Durand-Kerner per prime factor + Newton: `|f_n(t)|/n < 3e-17` |
| Python parity | `response()` and table samples match to 1e-12 / 1e-5 |

## What exists

| File | Role |
|---|---|
| `include/pratt_poly.hpp`, `src/pratt_poly.cpp` | exact polynomials, `response`, roots, analog poles, biquad cascade (`makeSections`) |
| `include/pratt_wavetable.hpp`, `src/pratt_wavetable.cpp` | band-limited single-cycle table builder, interpolated read |
| `include/pratt_presets.hpp` | the 11 presets and GM mapping |
| `include/pratt_engine.hpp`, `src/pratt_engine.cpp` | voices, envelopes, room, drums, filter bank |
| `tests/pratt_core_tests.cpp` | f_n(2)=n, cascade identity, root residuals, stability, section-vs-analog response, impulse decay, golden table values |
| `tests/pratt_params_tests.cpp` | parameter table consistency, settings round trip, clamping, every rejection path |
| `tests/pratt_engine_tests.cpp` | 128-program mapping, 440 Hz pitch, bend, sustain pedal, polyphony cap, free decay, drums, filter gain vs `response()`, `5*7 == 35*1`, crossfade on switch, Both mode |
| `include/pratt_params.hpp`, `src/pratt_serialization.cpp` | parameter table, settings -> engine mapping, text state |
| `src/dpf/` | `PrattPlugin.cpp`, `PrattUI.cpp`, `DistrhoPluginInfo.h` |
| `profile.ttl`, `README.md`, `docs/screenshot.png` | catalog profile, overview, UI capture |
| `tests/pratt_threading_tests.cpp` | worker-vs-audio concurrency replay; run under TSan too |
| `CMakeLists.txt` | core, DPF target and tests; included from the root as `DOWNSPOUT_BUILD_PRATT` |

Run standalone (no root build needed):

```
for t in core engine params; do
  g++ -std=c++17 -O2 -Wall -Wextra -UNDEBUG -Iinclude -I../../include \
      src/*.cpp tests/pratt_${t}_tests.cpp -o pratt_${t}_tests && ./pratt_${t}_tests
done
```

## Two uses of the same core

1. **Wavetable voice** (port of the Python). Build tables off the audio thread,
   cache by (pitch, preset, velocity bucket, dark). The full 128x11x8x2 set is
   too large to precompute at 4096 floats, so fill lazily on a worker and
   crossfade velocity buckets. Table build is O(harmonics x size) (about 0.2M
   multiply-adds).
2. **Pratt filter** (new). `makeSections(n, w0, fs)` gives a biquad cascade for
   any audio source, including the Python percussion path (noise through
   `H_{pitch+1}`). Two index controls `m`, `n` cascade as `H_m H_n = H_mn`.
   Integer index changes are discontinuous: crossfade two filters, never
   interpolate coefficients.

## Assumptions and open decisions

- Bilinear transform without prewarping; section response compresses towards
  Nyquist. Cutoff is clamped to 0.1 fs. Drum filters at 6 kHz are warped at 44.1 kHz.
  Add prewarping or oversampling if a high-cutoff filter mode is wanted.
- Parameter-change crossfade restarts if a second change arrives mid-fade.
- Presets are fixed data; user-editable presets would need state serialization.
- The new-plugin checklist in CLAUDE.md is done except host validation (see TODO.md).
