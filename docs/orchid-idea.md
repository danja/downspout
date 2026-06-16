# Orchid idea

Imagine if a system could transform spoken language into singing. That thought
is the origin of `orchid`.

Building a credible speech-to-song transformer would be a large research
project, but a simpler effect can still explore the musical part of the idea:
detect short tonal regions in incoming audio, freeze them as rhythmically timed
micro-loops, and blend those holds back into the live signal.

`orchid` should be a transport-aware audio effect VST. It takes audio input,
outputs audio, and reads host beat/bar/tick position. The first version should
not try to infer lyrics, phonemes, melody, or full vocal intent. It should treat
the input as a stream of possible pitched grains and make them bloom into held
tones when the signal is stable enough.

## Core Effect

The processor continuously writes incoming audio into a rolling buffer. In
parallel it runs a small voiced-region detector over the recent mono sum. When
the detector sees enough energy and periodicity inside a musical pitch range,
the processor captures a loop from the rolling buffer and enters a hold state.

During a hold:

- the captured loop is played repeatedly for a duration derived from the host
  grid;
- live input is either muted, mixed underneath, or written only to the capture
  buffer depending on the selected mode;
- loop wrap points and state transitions are crossfaded to avoid clicks;
- the output can optionally track a target note or scale by resampling the loop.

After the hold:

- the safest default is to crossfade back to live input;
- an optional "catch-up" mode can replay buffered live input, but it must be
  treated as a deliberate time effect because it either adds latency, compresses
  time, or drops material when the buffer is flushed.

This makes `orchid` closer to a rhythm-aware vocal freeze/resonator than a
transparent delay. That is a better first target because it can be implemented
deterministically and tested with synthetic signals.

## Detection

The detector should start simple and portable. A practical first pass is:

1. Convert the current analysis window to mono.
2. Remove DC and apply a Hann window.
3. Reject frames below an RMS threshold.
4. Estimate period with normalized autocorrelation across a bounded pitch range.
5. Require the best correlation peak to exceed a periodicity threshold.
6. Smooth accepted pitch and confidence over several windows.
7. Trigger capture only after the signal remains stable for a minimum time.

Suggested MVP pitch range:

- low: 80 Hz, roughly `E2`;
- high: 900 Hz, enough for most sung fundamentals and bright speech vowels;
- window: 40-60 ms;
- hop: 5-10 ms;
- stable time: 30-120 ms.

Autocorrelation is attractive here because it is dependency-free and robust
enough for monophonic voiced material. YIN-style difference functions can be a
later refinement if plain autocorrelation is too eager on noisy or breathy
sources.

The detector should output a compact status object, not directly change audio:

- `voiced`: whether the current window is usable;
- `confidence`: normalized periodicity confidence;
- `frequencyHz`: estimated fundamental;
- `rms`: current input energy;
- `stableFrames`: how long the current estimate has been stable.

Keeping this separate makes it possible to unit test pitch detection without
constructing the whole plugin.

## Capture

Once a voiced segment qualifies, `orchid` should pick a loop from the recent
buffer. A good first capture strategy is:

- center the capture near the latest stable analysis window;
- choose an integer number of estimated periods, not an arbitrary sample count;
- clamp the loop to a musically useful size, for example 20-160 ms;
- search a few samples around the proposed start/end for a lower-discontinuity
  join;
- store loop metadata: start frame, length, detected frequency, confidence, and
  capture beat position.

The loop does not need to be long. The perceived held tone comes from repeating
the most stable part of the vowel or pitched source. Shorter loops will sound
more synthetic, while longer loops retain more source identity.

Stereo input can share the mono detector while preserving stereo capture. The
detector uses the mono sum to decide timing and period; loop playback reads the
same frame range from each input channel.

## State Machine

The portable core should be a small state machine:

- `Pass`: live input passes through while the detector watches.
- `Armed`: the detector has found a candidate but stability is not long enough
  yet.
- `Held`: a loop is active and scheduled for a rhythmic duration.
- `Release`: the loop fades out or crossfades back to live input.
- `Cooldown`: new captures are blocked briefly to avoid rapid chatter.

Important rules:

- transport stop should return to pass-through and clear active rhythmic holds;
- missing transport should either pass through or use an explicit internal
  fallback clock, but the choice must be documented before implementation;
- captures should normally align their start or release to the selected grid;
- a new stronger voiced region may replace the active loop only if a retrigger
  mode allows it.

The MVP should prefer pass-through when transport is unavailable. That is less
surprising for an audio effect that can otherwise mute or freeze speech.

## Rhythm

All hold lengths should be expressed musically:

- grid: `1/4`, `1/8`, `1/16`, triplet options later;
- hold length: 1-8 grid units;
- release length: milliseconds or a fraction of the grid;
- cooldown: grid fraction or milliseconds.

The DPF wrapper can map `TimePosition` to a portable `TransportSnapshot`, as
`p-mix` and `rift` already do. The core should convert host position into an
absolute beat value and derive block boundaries from that. Tests should cover
loop boundaries, rewind, tempo changes, stopped transport, and missing BBT data.

## Resume Behavior

The original idea says that incoming signal is muted during the hold, sent to a
buffer, then resumed. There are three possible meanings:

- `Live return`: keep writing input to history, but after the hold crossfade to
  current live input. This is simple, real-time, and should be the default.
- `Delayed return`: output the buffered material after the hold. This preserves
  missed audio but makes the plugin a variable-latency delay.
- `Catch-up return`: replay buffered material faster until current time is
  reached. This is an audible time-compression effect and can sound interesting,
  but it should be opt-in.

The first implementation should use `Live return`. `Delayed return` and
`Catch-up return` can be later modes once the freeze behavior is stable.

## Parameters

Likely MVP parameters:

- `Sensitivity`: input level threshold and confidence bias.
- `Periodicity`: required autocorrelation confidence.
- `Stability`: how long pitch must remain consistent before capture.
- `Pitch Low` / `Pitch High`: detector search range.
- `Grid`: rhythmic unit for hold scheduling.
- `Hold`: number of grid units to sustain a loop.
- `Release`: crossfade out time.
- `Cooldown`: minimum time before another capture.
- `Loop Size`: captured periods or capture length bias.
- `Mix`: dry/wet balance.
- `Live Under`: how much live input remains during a hold.
- `Mode`: live return first; catch-up later.

Useful status outputs for the UI:

- detector confidence;
- detected pitch;
- active state;
- hold progress;
- capture age;
- whether transport is usable.

Following the controller-heavy pattern in this repo, these should be exposed as
read-only/status parameters so the UI reflects processor reality rather than
only the last automation value.

## Implementation Shape

The implementation should follow the existing `p-mix`, `e-mix`, and `rift`
shape:

- `plugins/orchid/include/orchid_core_types.hpp`
  Parameters, transport snapshot, audio block, detector status, loop metadata,
  state enum, and engine state.
- `plugins/orchid/include/orchid_engine.hpp`
  `clampParameters`, `activate`, and `processBlock`.
- `plugins/orchid/src/orchid_engine.cpp`
  Rolling buffer, detector, capture selection, state machine, loop rendering,
  and crossfades.
- `plugins/orchid/include/orchid_serialization.hpp`
  Stable text serialization for parameters.
- `plugins/orchid/src/dpf/OrchidPlugin.cpp`
  Thin DPF wrapper, parameter mapping, state, transport conversion, and stereo
  audio ports.
- `plugins/orchid/src/dpf/OrchidUI.cpp`
  NanoVG panel with detector meter, pitch readout, hold envelope, and parameter
  controls.

`rift` is the best source for rolling-buffer and block-scheduled audio
rendering ideas. `p-mix` is the simpler reference for transport conversion and
minimal wrapper structure. `orchid` should not share their engine code directly
unless a genuinely common abstraction emerges after implementation.

Keep the detector and capture code portable C++. Avoid FFT dependencies in the
first version. If later analysis needs spectral features, isolate that behind a
small detector interface rather than spreading dependency-specific code through
the engine.

## Testing Plan

The first tests should be deterministic core tests:

- unvoiced noise never triggers a hold at normal thresholds;
- a steady sine inside the pitch range triggers after the configured stability
  time;
- a sine below/above the pitch range does not trigger;
- changing frequency too quickly prevents capture;
- held output remains bounded and free of obvious discontinuities at loop wraps;
- stopped transport passes audio through and clears held state;
- rewind does not leave stale block timing active;
- tempo changes alter future hold durations without corrupting the active loop;
- stereo capture preserves channel differences while using one detector.

Synthetic sine, pulse, and noise buffers are enough for the initial suite. Manual
tests with spoken vowels can come after the core passes deterministic cases.

## Risks

- Autocorrelation may mistake resonant noise, guitar chords, or room tone for a
  stable voice. Confidence and stability thresholds need conservative defaults.
- Short loops can buzz. Crossfaded loop joins and period-aligned capture are
  essential, not polish.
- Muting live input during holds may make words unintelligible. The default
  should keep some dry signal or make the wet hold clearly intentional.
- Catch-up buffering is a different effect with harder latency expectations.
  It should not be mixed into the MVP.
- Host transport data is not always available. The fallback behavior needs to
  be boring and predictable.

## MVP

The smallest useful `orchid` is:

1. Stereo audio effect with pass-through on stopped or missing transport.
2. Rolling buffer and mono autocorrelation detector.
3. Period-aligned loop capture from stable voiced input.
4. Beat-grid hold duration with release crossfade.
5. Dry/wet mix and live-under control.
6. Text-serialized parameters.
7. Core tests for detection, state transitions, and transport edge cases.

Once that works, the more characterful features can be added: scale snapping,
formant-preserving pitch shift, catch-up return, MIDI note guidance, external
sidechain pitch targets, and performance gestures for freezing or suppressing
captures.
