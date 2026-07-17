# Tuney VST porting notes

Source baseline: Tuney 0.3.39 (`/home/danny/github/tuney`), an MIT-licensed
Python desktop/CLI instrument by Tom Ritchford.

The port keeps the source split between character mapping, scale-to-tuning
conversion, millisecond text sequencing, and sound generation. Those behaviors
are portable C++; DPF is only the VST3/UI shell. Focused DPF character input is
used instead of Tuney's process-wide keyboard listener. Composition and custom
musical definitions use the versioned `tuney_state` text contract; transient
typing uses the `ui_event` state channel and a bounded queue.

Internal audio uses Tuney's scale and tuning frequencies. Generated MIDI uses
ordinary note number, channel, velocity, and offset values, so microtonal
frequencies are not represented as pitch bends. Text playback uses the plugin
sample clock and Tuney-style seeded millisecond timing rather than host BBT.

The v1 expression grammar accepts numeric literals, `+ - * / % ^`, parentheses,
and `cents(...)`. Python module/function expressions and random evaluation are
intentionally excluded so saved state is portable and deterministic.

The Python CLI, device selection, offline rendering, recording, speech, global
background typing, file browsers, and desktop autosave/history are outside the
plugin boundary.

DPF exposes a MIDI input for synth-category compatibility, but Tuney VST ignores
incoming host MIDI in v1; its UI text/typing interface is the note source.

## Text examples

- `Aa` produces two different notes with the default case-sensitive alphabet.
  `A` is the first mapped character and `a` is the twenty-seventh, so the two
  characters land in different parts of the configured note range. Disabling
  case sensitivity makes their interpretation equivalent.
- `Hi, A` plays notes for `H`, `i`, and `A` in that order. The comma and space
  do not produce notes with the default alphabet; instead, they add their
  configured silent delays. The seeded character durations, timing scale, and
  overlap setting determine the exact note starts and holds.
