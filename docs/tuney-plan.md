# Tuney VST port plan

Tuney VST is a portable C++20 reimplementation of Tuney 0.3.39's musical
core. It produces stereo audio and MIDI from focused UI typing or stored text,
while preserving character mapping, flexible scales and tunings, seeded
millisecond timing, basic oscillators, polyphony, replay, looping, and presets.

## Architecture

- Keep mapping, tuning, scheduling, synthesis, MIDI generation, and state in a
  plugin-local portable core with deterministic tests.
- Use a thin DPF wrapper for VST3 audio/MIDI and a custom NanoVG UI for Unicode
  typing, text display, clipboard paste, presets, and playback controls.
- Persist variable-length configuration and composition data in a versioned,
  escaped text state. Apply UI events through a bounded queue and keep parsing,
  allocation, locks, filesystem access, and unbounded work outside audio work.
- Preserve Tuney's millisecond timing independently of host transport. Internal
  audio honors microtonal tuning; MIDI remains ordinary note-on/off without MPE
  or pitch bends, matching the source program.

## Delivery

- Add `plugins/tuney-vst`, core tests, the `tuney_vst.vst3` bundle, local and
  release build integration, technical porting notes, root documentation, a
  Pages product entry, and a real UI screenshot.
- Port the white-notes, just-14, ambient-text, and MIDI-controller starting
  points without adding a new dependency.
- Omit the Python CLI, offline rendering, audio recording, speech, audio-device
  selection, global keyboard hooks, Scala browser, and desktop autosave/history
  from the first plugin release.

## Verification

Test mapping and limiter modes, computed/ratio/table tuning, UTF-8 state,
seeded scheduling, oscillator and envelope behavior, voice stealing, bounded
audio, MIDI settings and cleanup, replay/loop/reset behavior, fresh CMake
configuration, the VST3 target, release-script syntax, bundle consistency, and
the screenshot/catalog workflow.

Tuney is MIT-licensed and copyright Tom Ritchford. Ported behavior retains that
attribution in the plugin documentation.
