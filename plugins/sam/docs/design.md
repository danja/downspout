# Sam: design and porting notes

Sam is a new plugin built around an existing program, not a port of an LV2 plugin, so there is no
source plugin whose behaviour has to be preserved. What is preserved is the speech: SAM's text
reader, phoneme rules, frame interpolation and synthesis are the vendored C, changed only where it
could crash or hang (`third_party/sam/PATCHES.md`).

## Shape

```
third_party/sam/*.c      SAM, plain C, built as downspout_sam_vendor (warnings off, PIC)
include/sam_params.hpp   parameter table, enum names, voice presets, pitch constant
src/sam_speech.cpp       renderWord(): a C++ face for SAM; script parsing and text cleaning
src/sam_engine.cpp       Engine: voices, scheduling, MIDI, transport, tone and level
src/dpf/SamPlugin.cpp    thin DPF wrapper: parameters, the "text" state, transport and MIDI plumbing
src/dpf/SamUI.cpp        panel on the shared Magneto kit, plus the text editor
tests/sam_core_tests.cpp 36 deterministic tests; the core never touches DPF
```

DSP and domain logic stay in the core; the wrapper only translates host types.

## SAM, as used

SAM turns text into phonemes (the *reciter*), then phonemes into 10 ms frames of three formant
frequencies and amplitudes plus a glottal period, then into an 8-bit, 22.05 kHz buffer. It keeps
all of its state in globals and writes one static buffer, so `renderWord()` takes one process-wide
lock; two plugin instances therefore take turns, each for tens of microseconds.

A **word** (at most 48 characters) is the unit of rendering. SAM limits a phoneme string to about
120 characters, and one word per note or step is what the musical modes need anyway. Rendering
happens on the audio thread when a word starts, straight into a preallocated voice buffer.
Measured with `-O2`: 30 to 50 us for a short word, which is why no worker thread or cache was
built. Rendering is deterministic (a patched `Output()` no longer remembers the previous render),
so the same word at the same settings gives the same samples.

### Pitch

In singing mode SAM's pitch byte is the glottal period in ticks, each advancing the output by 162/50
samples, so `f0 = 22050 * 50 / 162 / pitch = 6805.56 / pitch`. This was measured before it was used
(autocorrelation of a sustained vowel at pitch bytes 20 to 100 gave 6784 to 6819 times the byte).

A whole number of ticks is coarse at the top of the keyboard, so a note picks the nearest pitch byte
and the voice is resampled by `target / actual`. Measured over notes 28 to 96, the result is within
12 cents of the key (the estimator, not the plugin, limits that near B6). The price is that formants
move by the same ratio: under 4 % up to C5, about 13 % near B6, where the byte is clamped at 3.

Transport mode does not retune: it uses the **Pitch** parameter directly. With **Monotone** off SAM
applies its own contour (stress, and rising or falling inflection on `?` and `.`); with it on the
word is sung at one pitch.

## Scheduling in Transport mode

Position is `bar * beatsPerBar + barBeat` from the host's BBT (zero-based). Relative to the start bar
it is `rel`, and a slot is `step` beats long (`1 bar` and up follow the time signature).

- **Onsets.** Slot `s` starts at the first frame at or after `s * step`. Rounding to the nearest frame
  instead puts the onset one frame early or late depending on where the host cuts its blocks, which a
  block-size test caught; with this rule output is bit-for-bit independent of block size.
- **Once each.** `lastTriggered_` counts slots already started, so floating-point wobble at a block
  boundary can neither skip nor repeat a word.
- **Relocation.** A block whose position is more than 0.01 beat from where the last one ended (hosts
  report whole ticks), or the first block after stopping, is a locate: running words get an 8 ms
  fade, and if the playhead is part-way through a slot, that word starts at the matching point
  (`offset beats * 60 / bpm` seconds into it). A loop wrap is a locate.
- **Stop.** Stopping the host fades everything. Mid-song tempo and time-signature changes are
  followed block by block.
- **Speak.** Run without a transport: an internal playhead starts at the start bar and runs once
  through the text at the host tempo (120 if there is none), then stops. A running host takes over.
- **No valid BBT** (some standalone hosts): silent, as for a stopped transport. Documented, not
  worked around.

Keys mode ignores the transport, apart from one thing: the "next word" pointer returns to the first
word when the host starts playing, so a part lines up with the text on every pass.

## Threads and state

- **Audio thread**: `process()` reads parameters (relaxed atomics), renders words, mixes. It allocates
  nothing (the test counts allocations over 400 blocks of mixed Keys and Transport work) and never
  blocks, except on SAM's lock, held for microseconds.
- **Text**: a state, `text`, with `kStateIsHostWritable` and `DISTRHO_PLUGIN_WANT_FULL_STATE`, so it
  is saved in the project. `setState` (main thread) parses the text into a `Script` and hands it over
  under a mutex; the audio thread takes it with `try_lock` at the start of a block and keeps the old
  script for a block if the lock is busy. The retired script is freed on the main thread. Nothing
  else is state: every setting is a parameter, which DPF saves by itself.
- **Lifecycle**: `activate()` and `sampleRateChanged()` clear voices, filters and the scheduler's
  position, never the text or the parameters (`testActivationKeepsThePatch`).
- **Status**: the read-only `out_word` parameter carries the slot being spoken, so the panel can
  highlight it. DPF has no path from the plugin to push a parameter value back to the panel, which is
  why the highlight comes from an output parameter, as in Magneto.

## Assumptions for the host mapping

- Transport comes from `getTimePosition().bbt`; `playing` and `bbt.valid` must both be true.
- Output is a mono voice copied to both channels; the audio port is a stereo group.
- The VST3 category is Instrument | Synth. `DISTRHO_PLUGIN_IS_SYNTH` is set, as for Magneto.
- MIDI sysex and events longer than three bytes are dropped.
- Not validated in a plugin host yet (see `TODO.md`). Everything above is tested through the core;
  the wrapper was also run as the JACK standalone under Xvfb, driven by a throwaway client that sent
  MIDI and acted as timebase master with BBT. Keys mode spoke the right words at the right times;
  Transport output, recorded and cross-correlated against the core's offline render, matched at a
  constant lag (0.989 to 1.000 over four windows, 0.3 ms of jitter, JACK's tick resolution); and the
  panel highlighted the word being spoken. Typing, caret movement, wrapping and mode switching in the
  text box were driven with xdotool.

## Not done

- Per-note formant correction after the retune (a shifted voice stays shifted).
- Sustain pedal, mod-wheel or other CC mapping.
- A phrase mode that speaks the whole text on one note; words are the unit.
- Audition in Keys mode (Speak is Transport mode only).
- Word timing that stretches to fit a step (SAM's speed is a render setting, not a duration).

## Tests

`tests/sam_core_tests.cpp`, built with `-UNDEBUG` and `downspout/test_assert.h`. Besides the usual
parameter and script checks: renders are deterministic and independent of what came before;
pitch is within 1.5 % at eight keys from A1 to E6 at 48 kHz, and at A3 from 22.05 to 192 kHz; notes and words start on
their frame; release time matters; velocity scales level; the word cycle, Key = word and channel
filter pick the right word; pitch bend and all-notes-off work; words land on their steps at several
tempos, step sizes and time signatures; start bar, loop and stop behave; a locate speaks the
rest of the word, matching the same stretch played from the start; output is identical for block sizes 1
to 4096; every control changes the output in the scene where it applies; the audio path allocates
nothing; 4,000 random strings render without incident; the text can change under a running audio thread.

Also run under AddressSanitizer and UndefinedBehaviorSanitizer by hand (compile the C and C++
separately; do not use `stdbuf`, which defeats ASan's preload check).
