# Energise implementation plan

Status: first implementation exists; Phase 5/6 exit criteria remain pending. The
product concept is in `docs/drumloop-energise-overview.md`; this document records
the implementation steps, tests and exit criteria. Current progress is tracked in
`docs/energise-progress.md`.

## Scope boundary

The first version is an **offline tool inside a VST3 plugin**, not a
zero-latency insert. It captures a short drum break from the inputs (or loads a
built-in demo), produces an editable event list, quantises it to the host BPM
grid, renders a replacement audio buffer, and loops the result in BBT sync. A
neural classifier or neural renderer is an optional
later backend, not part of the first buildable milestone.

Do not vendor a model runtime, WAV library or training data without explicit
approval. Keep all portable logic free of DPF headers and put host glue in the
wrapper.

## Implementation shape

Suggested files:

```text
plugins/energise/
  CMakeLists.txt
  profile.ttl
  include/energise_core.hpp
  include/energise_params.hpp
  src/energise_core.cpp
  src/analysis/onsets.cpp
  src/analysis/tempo_grid.cpp
  src/analysis/classifier.cpp
  src/synth/voices.cpp
  src/render/renderer.cpp
  src/dpf/EnergisePlugin.cpp
  src/dpf/EnergiseUI.cpp
  tests/energise_core_tests.cpp
  docs/design.md
```

Register tests with `downspout_add_core_test`. Include
`downspout/test_assert.h` so Release builds cannot silently compile assertions
away.

### Core data model

`include/energise_core.hpp` should define the stable language of the tool:

```cpp
enum class DrumClass : std::uint8_t {
    Kick, Snare, Clap, Rim, ClosedHat, OpenHat, Ride, Crash,
    LowTom, MidTom, HighTom, Percussion, Unknown, Count
};

struct DrumEvent {
    std::uint32_t id;
    double sourceTime;
    double quantisedTime;
    double sourceDuration;
    DrumClass primary;
    std::array<float, static_cast<std::size_t>(DrumClass::Count)> scores;
    float velocity;
    float confidence;
    bool ghost;
    bool muted;
    float pan;
};

struct TempoSegment { double startSec; double bpm; };

struct GrooveMap {
    double bpm;
    double phaseSec;
    int gridDivisionsPerBeat;
    float swing;
    std::vector<TempoSegment> drift;
    float confidence;
};

struct RenderSettings {
    float quantiseStrength;
    float ghostStrength;
    float swing;
    float humanise;
    std::array<float, static_cast<std::size_t>(DrumClass::Count)> classOffsetsMs;
    std::array<float, static_cast<std::size_t>(DrumClass::Count)> classGains;
    float transientLevel;
    float bodyLevel;
    float residualLevel;
    float saturation;
    float variation;
    float outputTrim;
};

struct CaptureBuffer {
    int sampleRate;
    std::vector<float> left;
    std::vector<float> right;
};

struct RenderedBuffer {
    int sampleRate;
    std::vector<float> left;
    std::vector<float> right;
};
```

Keep event IDs stable across edit, render and state save/load. `primary` drives
the UI marker colour and MIDI output; `scores` keeps the classifier's uncertainty
available when the user reviews a hit.

---

## Part 1 — Workflow: capture, review, render, A/B

### Steps

1. Define an explicit processor state machine:
   `Idle → Captured → Analysed → Quantised → Rendered`, with `Error` reachable
   from any stage. Store it in an output parameter so the UI can render it.
2. Add ordinary integer request counters, not host trigger parameters:
   `capture_request`, `analyse_request`, `render_request`, `ab_swap`, `undo`,
   `redo`. DPF/VST3 suppresses typical trigger writes, so one-shots must commit
   ordinary values.
3. Implement core entry points:
   - `Processor::capture(const CaptureBuffer&)`
   - `Processor::analyse()`
   - `Processor::quantise()`
   - `Processor::render()`
   - `Processor::setEvent(std::uint32_t id, const DrumEvent&)`
4. Arm `Capture Bars` (1–4, default 2) and record inputs 1/2 from the next host
   bar line, using `getTimePosition()` BBT for the start and host BPM for the
   grid. DPF has no reliable host time-selection API, so do not promise to follow
   a DAW selection. Add WAV import later behind a portable reader if approved.
5. Make analysis/render synchronous in the core. The wrapper may show progress
   through `status_progress`; if it grows beyond a UI-blocking delay, run it in
   bounded idle slices or a worker later. Do not put DSP logic in `exec_idle()`.
6. Add optional MIDI export from the edited event list, using GM drums on
   channel 10: kick 36, snare 38/40, clap 39, closed hat 42, open hat 46, toms
   45/47/50, crash 49, ride 51. Emit a short note-off for every note-on so hosts
   release the voice. MIDI Out gates it; the wrapper refreshes the pattern after
   analyse/render/edits, and BBT Loop repeats it with the audio loop while the
   transport rolls. Build it offline and flush from `run()`.
7. Audio Out selects Off (default), Pass (monitor inputs) or Render (loop the
   buffer); A/B swaps the Render buffer without changing the event list.
   Automatic replacement of a DAW media item is out of scope.

### Tests

- state machine rejects `render_request` before a capture;
- request counters round-trip through `setParameterValue()/getParameterValue()`;
- capture bars clamp to the published range and to the hard 8 s cap;
- BBT helpers derive bar length, phase and next-bar delay from host tempo;
- events survive edit → quantise → render with stable IDs;
- A/B swap changes monitored buffer but not events;
- MIDI export emits each hit as a channel-10 note-on followed by a note-off,
  with the expected notes and velocities.

### Exit criteria

A user can get from bar-aligned capture to a BBT-locked rendered A/B loop, and
every stage plus BBT/output-switch state is observable from output parameters.

---

## Part 2 — Analysis: onset detection, host-locked grid and classification

### Steps

1. Write the accuracy probe before the DSP: generate a synthetic break with
   known kick/snare/hat/tom times, including jitter, ghost notes, overlaps and
   cymbal tails. Store expected labels independently of the implementation.
2. Implement `OnsetDetector`:
   - mono sum plus mid/side features; remove DC;
   - STFT frames at 1024/2048/4096 samples with 128/256-sample hops;
   - band splits around 120 Hz and 2 kHz;
   - spectral flux and phase-deviation novelty per band;
   - adaptive threshold and local maxima;
   - parabolic interpolation, then waveform-domain attack refinement within
     ±10 ms;
   - merge duplicate onsets inside 8 ms while preserving multi-label scores.
3. Keep `TempoGridFitter` as a swappable offline utility, but do not use it in
   `Processor::analyse()`: the wrapper supplies the host BPM and bar-line phase
   and `analyseCaptureWithGroove()` keeps that grid verbatim. Drift fitting is
   disabled in the processor so the loop cannot wander from the DAW tempo.
4. Implement `DrumClassifier` with a feature extractor first:
   - band energy and decay slope in 5/20/100 ms windows;
   - spectral centroid, flatness, rolloff and high/low ratios;
   - low-band pitch contour via autocorrelation;
   - noise ratio, stereo width/pan, attack sharpness, tail length and velocity;
   - rule scores for the obvious cases (low pitch drop → kick; wideband snap →
     snare; short high-band decay → closed hat; long high-band decay →
     cymbal); ghost flag from velocity below a threshold relative to the same
     class.
5. Keep the classifier behind an interface so a gradient-boosted tree or ONNX
   model can replace the rules later without changing the event list.
6. Source separation is optional and deferred. If an STFT mask is ever used in
   the render path, first prove a unity-mask round trip equals the input delayed
   by the reported latency.

### Tests

- onset detection on the synthetic fixture with 30 ms tolerance: precision and
  recall at least 0.85;
- no duplicate onsets for simultaneous kick+hat; they become one multi-label
  event;
- sub-sample refinement beats the raw hop estimate on a deliberately shifted
  click;
- host-locked analysis keeps the supplied BPM/phase exactly and emits no drift;
- capture frames and effective bar count respect the 8 s cap at slow tempos;
- class accuracy at least 80 % on synthetic labels;
- ghost notes flagged at low velocity while keeping their class;
- stopped/silent capture produces no events and no NaN confidence.

### Exit criteria

The analyser emits a reviewable event list with useful timing and plausible
labels before any resynthesis exists.

---

## Part 3 — Quantisation: macro grid plus micro feel

### Steps

1. Convert the chosen grid division and BPM into target sample times.
2. Apply swing before quantisation so the value is part of the target grid.
3. For each event:
   `quantisedTime = sourceTime + strength * (targetTime - sourceTime)`.
4. Use a separate `ghostStrength`, default lower than the main strength, and
   allow per-class offsets in milliseconds.
5. Apply `humanise` only after quantisation, from a seeded PRNG so renders are
   deterministic.
6. Never quantise the marker's `id`, class or confidence. Keep
   `sourceTime` untouched so the UI can draw the original → quantised arrow.
7. Clamp all times to the capture bounds while allowing cymbal tails to extend
   into the render tail.

Suggested defaults: 1/16 grid, 70 % strength, 0 swing, ghost strength 35 %,
humanise 0, all class offsets 0 ms.

### Tests

- strength 0 leaves every time unchanged;
- strength 1 snaps every non-ghost event exactly to the grid;
- ghost note moves by `ghostStrength`, not the main strength;
- swing shifts offbeat 16ths without moving downbeats;
- per-class offset moves only that class;
- humanise stays inside its published range across a full sweep;
- render determinism with the same seed and state.

### Exit criteria

Quantisation is audible, reversible through editing, and never destroys the
original timing evidence.

---

## Part 4 — Resynthesis: transient, body, residual and space

### Steps

1. Implement `VoiceRenderer` as a portable function of `(DrumEvent,
   RenderSettings, seed)` returning a short stereo buffer.
2. Build the transient layer first: 1–3 ms attack, class-specific noise/click
   filter, deterministic seed from `event.id`.
3. Add class bodies:
   - kick: pitch-dropping sine, click and gentle saturation;
   - snare: noise band plus tuned 180–250 Hz body;
   - clap/rim: short noise bursts with tighter filtering;
   - hats: filtered noise and metallic partials;
   - toms: pitched drop with noise attack;
   - cymbal: partial bank with long stereo decay.
4. Apply oversampled saturation to nonlinear paths. Flush tiny values per
   block so idle voices do not decay into denormals.
5. Add the residual/texture layer: take a short filtered fragment of the
   original hit, time-snapped to the quantised position and crossfaded. This
   keeps kit character while the transient/body layers add detail.
6. Add per-class gain, pan, width and tail length. Render into a fresh
   `RenderedBuffer`; do not mutate the capture.
7. Provide seeded per-hit variation in level, tuning and noise phase. Keep the
   range small enough that repeated hits stay recognisably the same kit.
8. Trim output to the published ceiling; assert no clipping across a sweep of
   saturation and output trim.

### Tests

- each voice emits finite samples and decays to −60 dB within its published
  tail length;
- velocity changes level in every class;
- transient level affects the first 5 ms without moving the body onset;
- residual level 0 uses no source fragment, level 1 includes it;
- saturation sweep checks decay, not just peak level;
- alias probe measures high-frequency energy before/after oversampled
  saturation;
- same state and seed produce bitwise identical renders;
- no NaN or infinities across the full parameter space.

### Exit criteria

The render sounds cleaner than the low-quality input while still recognisable,
and the renderer is deterministic and stable.

---

## Part 5 — UI: waveform/event lane

### Steps

1. Follow `plugins/magneto` for panels, sliders, switches and dropdowns. Add one
   novel component, `EventLane`, rather than inventing a second framework.
2. Draw the waveform, grid, event markers, original → quantised arrows and
   playhead. Use marker colour for class, size for velocity, opacity for
   confidence and hollow markers for ghosts.
3. Add interactions:
   - click selects;
   - drag changes timing;
   - shift-drag changes velocity;
   - wheel or keys change class;
   - modifier toggles ghost;
   - A/B button swaps the Render buffer;
   - Audio Out 3-way switch, MIDI Out and BBT Loop switches, capture-bar stepper;
   - per-class mute/solo chips;
   - undo/redo buttons.
4. Keep global settings as ordinary automatable parameters appended in a
   stable order. Status outputs go after them and drive readouts.
5. Do not append editable controls beyond the read-only output block unless the
   UI handles them against its own full value array and uses `sendValue()`;
   `MagnetoUI` mirrors only `kMaxValues` (64) parameters.
6. Add a generous lower margin (~60–80 px) and check label/value fit in the
   real capture.
7. Add `energise` to `scripts/capture-plugin-screenshots.sh` and inspect the
   capture from a first-time-user perspective before accepting it.

### Tests

- UI mapping assertion: every control maps to a real parameter or request
  counter;
- event hit-testing selects the nearest marker, not the lane background;
- drag clamps to capture bounds;
- class change updates marker colour;
- ghost toggle updates marker shape;
- status outputs repaint the selected-event readout.

### Exit criteria

The workflow is understandable without source code: capture, analyse, correct
events, render and compare are all visible.

---

## Part 6 — State, exports and live status

### Steps

1. Use `WANT_STATE` with `WANT_FULL_STATE`; `Plugin`'s third constructor argument
   is *stateCount*, set to 1. Run `scripts/check-plugin-state.sh`.
2. Define a versioned serialization contract:
   - `capture.version`;
   - sample rate, channel count and frame count;
   - PCM payload in base64 chunks;
   - events as binary structs in base64;
   - groove and render settings;
   - selected event and A/B side.
3. Keep global settings in parameters so the host saves them normally; state is
   only for the captured buffer, event list and editor selections.
4. Emit output parameters for `state`, `progress`, `event_count`,
   `selected_class`, `selected_confidence`, `render_ready`, `ab_side`, `tempo_bpm`
   and `bbt_valid`. DPF's VST3 bridge
   has no general DSP→UI state callback, so live readouts must come from output
   parameters.
5. Test save/reopen in a real host after the core tests pass. Core tests do not
   touch the DPF wrapper.

### Tests

- state round-trip preserves sample rate, PCM, event IDs, edits, groove and
  settings;
- missing or older version keys fall back to Idle without crashing;
- activation and sample-rate changes clear runtime playback state but not the
  captured patch;
- host save/reopen retains the event list and selected event.

### Exit criteria

A project reopens with the same captured break, event edits and render result.

---

## Part 7 — Optional ML and separation

### Steps

1. Keep `DrumClassifier` and `VoiceRenderer` interfaces fixed before adding a
   model.
2. Validate a classical baseline first; only collect/train data after the
   synthetic and real-break fixtures show where rules fail.
3. If approved, add ONNX Runtime behind an optional CMake option with a clear
   licence/build-size note. Do not link it by default.
4. Store model identity and version in state so old projects can report a
   missing model rather than silently changing results.
5. A neural residual renderer, if attempted, must be conditioned on class,
   velocity and a timbre embedding from the original hit and must preserve
   deterministic offline render.
6. Source separation can be introduced as an analysis preprocessor to produce
   cleaner features and per-class references; it must not become a dependency
   of the basic path.

### Exit criteria

An optional backend improves measured labels or audio quality without changing
the event-list contract or breaking hosts that do not have it.

---

## Part 8 — Plugin wiring, docs and release checklist

When the first buildable plugin exists, complete the new-plugin checklist in
`AGENTS.md`:

1. add `plugins/energise/CMakeLists.txt`, the root CMake build option and the
   `install(DIRECTORY ...vst3)` rule;
2. update `install.sh`, `scripts/package-release.sh` and
   `.github/workflows/release.yml`;
3. update `README.md`, `docs/install.md`, `docs/release.md`,
   `docs/architecture.md`, `docs/plan.md` and `docs/pages/plugins-summary.md`;
4. add `docs/pages/_products/energise.md`, add `energise` to
   `scripts/capture-plugin-screenshots.sh`, generate
   `docs/pages/assets/plugins/energise.png`, and visually accept it;
5. create `plugins/energise/profile.ttl` with every parameter, range, default,
   MIDI/audio routing and state key;
6. run core tests, the `-vst3` target, shell syntax checks and
   `scripts/check-bundle-lists.sh`.

---

## Phase milestones

| Phase | Work | Exit criterion |
| --- | --- | --- |
| 0 | Approve scope: capture path, max capture length, ML dependency policy | Decisions recorded here and in plugin docs |
| 1 | Core data model, fixtures, state machine | Tests pass with no audio DSP |
| 2 | Onset + host-locked grid + event list | Synthetic accuracy targets met; processor keeps host BPM |
| 3 | Rule classifier + MIDI export | Labels reviewable and editable |
| 4 | Transient/body/residual renderer | Deterministic, stable A/B render |
| 5 | Waveform/event UI | First-time-user screenshot review accepted |
| 6 | State and host validation | Save/reopen and REAPER render verified |
| 7 | Optional ML/separation | Improvement measured; default build unchanged |

## Commands

Run configure to completion before starting a build in a fresh directory.

```bash
cmake -S . -B /tmp/opencode/energise-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/opencode/energise-build --target downspout_energise_core_tests -j2
ctest --test-dir /tmp/opencode/energise-build -R energise --output-on-failure

cmake -S . -B /tmp/opencode/energise-vst3-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/opencode/energise-vst3-build --target energise-vst3 -j2

scripts/capture-plugin-screenshots.sh energise --output-dir /tmp/opencode/energise-shots
bash -n install.sh scripts/package-release.sh
scripts/check-bundle-lists.sh
scripts/check-plugin-state.sh
```

Filter large build logs with `grep -E "error:|Error [0-9]"` and check the exit
status. After adding a test, confirm the test count or runtime changed before
claiming it ran.

## Decisions

- **Capture source:** record 1–4 whole bars from the next host bar line on inputs
  1/2, with a built-in demo break. No WAV import in this pass; host time selection
  is not available through DPF.
- **Maximum capture length:** hard 8 s cap on PCM state; slow tempos use fewer bars.
- **Classifier baseline:** portable rules only; GBT/ONNX backends stay swappable
  and require explicit approval before any model or runtime is vendored.
- **MIDI output:** channel-10 export gated by MIDI Out and repeated with the BBT
  loop; no real-time playable event mode.
- **Audio output:** Off/Pass/Render with BBT loop sync; automatic DAW media-item
  replacement is not planned.
