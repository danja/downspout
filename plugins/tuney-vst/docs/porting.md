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
sample clock and Tuney-style seeded millisecond timing by default.

`SYNC ON` replaces that free-time scheduler with a deterministic host BBT grid.
Mapped characters and spaces occupy one sixteenth-note slot, commas one
eighth-note slot, periods/colons/semicolons one quarter-note slot, and blank-line
rests four quarter-note beats. Rate scales this beat timeline. Timing Scale,
Timing Seed, Overlap, Minimum Note, and the punctuation millisecond fields do
not determine scheduled note/rest lengths while sync is enabled. Host play
starts the stored text from its beginning, host stop silences it, and host
restart, rewind, loop, or a discontinuous bar/meter change restarts it. The
plugin's Loop control repeats the text on its own beat-length boundary. PLAY
re-arms/restarts synced playback; STOP disarms it until PLAY or sync is enabled
again. Live typed notes remain immediate rather than quantized.

The v1 expression grammar accepts numeric literals, `+ - * / % ^`, parentheses,
and `cents(...)`. Python module/function expressions and random evaluation are
intentionally excluded so saved state is portable and deterministic.

The Python CLI, device selection, offline rendering, recording, speech, global
background typing, file browsers, and desktop autosave/history are outside the
plugin boundary.

DPF exposes a MIDI input for synth-category compatibility, but Tuney VST ignores
incoming host MIDI in v1; its UI text/typing interface is the note source.
While the plugin UI has keyboard focus, printable typing is consumed by the
editor so host shortcuts such as REAPER's Space transport action do not fire.

## Text examples

- `Aa` produces two different notes with the default case-sensitive alphabet.
  `A` is the first mapped character and `a` is the twenty-seventh, so the two
  characters land in different parts of the configured note range. Disabling
  case sensitivity makes their interpretation equivalent.
- `Hi, A` plays notes for `H`, `i`, and `A` in that order. The comma and space
  do not produce notes with the default alphabet; instead, they add their
  configured silent delays. The seeded character durations, timing scale, and
  overlap setting determine the exact note starts and holds.
