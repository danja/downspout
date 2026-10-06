# Plank design notes

Plank re-implements the synthesis engine of the open-source
[Plinky](https://plinkysynth.com) touch synth as a portable C++ core, then adds a
Launchpad grid playing surface and a NanoVG UI. This file records what was
taken from Plinky, what was deliberately left behind, and where Plank departs
from the original.

## Why a rewrite rather than a port

`plinky_public/sw/Core/Src/plinky.c` is 3400 lines built around the STM32L4
runtime: it is a single translation unit including HAL headers, DMA handles,
touch-controller calibration, an OLED UI, WebUSB and a bootloader check. Its
`tables.h` alone is 2189 lines of precomputed lookup data.

Porting that file would have dragged the hardware layer into the plugin and
pushed the code away from every other plugin in this repository. So the DSP is
reimplemented in the portable-core / thin-DPF-wrapper shape that `p-mix`, `e-mix`
and `rift` established, and the shared code contract is:

```
include/plank_params.hpp   parameters, scales, Launchpad map  (no DSP)
include/plank_voice.hpp    oscillators, filter, envelopes, LFOs
include/plank_core.hpp     Processor: grid, voice allocation, effects
src/plank_core.cpp         the core
src/plank_voice.cpp        voice primitives
src/plank_wavetables.cpp   band-limited table synthesis
src/dpf/                   DPF wrapper and NanoVG UI only
tests/                     deterministic core tests
```

Plinky's software is MIT licensed (`LICENSE.md`), which permits this. Attribution
is recorded in `README.md` and here.

## What came across

| Plinky | Plank |
|---|---|
| `RunVoice` two-oscillator pair | `Processor::renderVoice` |
| polyBLEP saw edges | `polyBlep` in `plank_core.cpp` |
| y1/y2 resonant filter with 0.999 leak | `ResonantFilter` |
| drive compensated by `2 / (resonance + 2)` | `driveGain` in `renderVoice` |
| two ADS envelopes | two `Envelope` instances |
| Cytomic dynamic knob smoothing | `Smoother` |
| 11 LFO shapes | `Processor::lfoSample` |
| scale quantised pitch lookup | `kScaleIntervals` plus `noteForCell` |
| tape delay, shimmer reverb, compressor, M/S width | `renderDelay`, `renderReverb`, `applyOutputStage` |
| eight fingers as eight voices | eight grid columns as eight strings |
| strings tuned apart, like a guitar | `Spread` separates the columns |

## What was left behind, and why

- **Granular sampler.** Needs sample loading and storage; out of scope for a
  grid instrument and would have dominated the parameter surface.
- **Arpeggiator and step sequencer.** Deliberate: the brief was the voice engine
  and the grid. Plinky's `arp.h` is a good candidate for a later addition.
- **Touch input, accelerometers, ADC/CV.** These are how Plinky gets its gestures;
  the Launchpad grid replaces them.
- **Oscillator, OLED and LED UI, WebUSB, flash presets, recording.** Host and UI
  concerns that this repository already handles.

## Deliberate departures

**Filter coefficient is separate from VCA gain.** In Plinky the envelope drives
both at once: `y1 += (... - y1) * vol`, so the filter opens as the note decays.
Plank keeps Plinky's recurrence but feeds it a dedicated cutoff coefficient and
applies the envelope as output gain. Driving both from the amplitude made the
cutoff control nearly inaudible, which is a bug rather than a feature.

**Wavetables are synthesised, not copied.** Plinky ships seventeen band-limited
Miunau tables as generated `int16` data in `wavetable.h`, with no generator in the
repository. Rather than vendor 17527 numbers, `plank_wavetables.cpp` builds the
same seventeen-table sweep by additive synthesis on first use. The classic shapes
(saw, three pulse widths, square, triangle, and sparse two/three/five-partial
forms) are generated from their exact Fourier series; the remaining nine step a
harmonic series down toward a near-sine. Phase scatter uses a fixed LCG so the
tables are bit-identical on every run and in every host.

**`Morph` is one continuous axis.** In Plinky `P_PWM` selects a table and offsets
the oscillator pair. Here a single control both crossfades away from the
polyBLEP oscillators and walks the table pair, so the sweep from buzzy saw to
round harmonic tone is one gesture rather than two coupled ones.

**Grid rows are a scale ladder, not a keyboard.** Plinky maps a finger's position
along a string to pitch, continuously. A Launchpad has discrete pads, so each
column instead gets one octave of the current scale across its eight rows, with
`Rotate` shifting which degree sits under which row and `Stride` pushing the whole
ladder by a constant interval. On top of that, `Spread` separates the columns the
way Plinky's strings are separated, which is what makes the eight voices sound
like eight strings rather than one string played eight times.

**Microtune is cents.** Plinky's `P_MICROTUNE` is a fine pitch offset in cents.
Plank keeps that meaning and applies it as a fractional-semitone detune, so the
8-cent default is a subtle chorus rather than a transposition.

## Launchpad contract

Taken from the `lifeform` plugin, which is the reference for this repository:

- programmer mode on entry, SysEx `F0 00 20 29 02 0D 0E 01 F7`;
- note-on channel 1 for static grid LEDs, channel 1 CC for side and top buttons;
- pad and control input is consumed, never echoed, because a release echoed back
  reads as an LED-off command;
- musical MIDI defaults to channel 4 to stay clear of the LED channels;
- a 0.75 second periodic LED refresh with change detection, so a Launchpad that
  missed an update recovers without flooding the port;
- panic sends programmer-mode SysEx, a bulk LED clear, and ordinary LED-off
  messages, the last because some hosts filter SysEx.

## Parameters

122 total: 48 host-writable controls, 64 grid cells, 10 read-only status.

The grid cell parameters exist so the UI and host automation can pluck a string
without a MIDI port. They are edge triggered: writing the same value twice does
not re-articulate, so a host restoring state cannot retrigger every note. MIDI
generated by an out-of-band parameter change is queued and flushed at the start of
the next block rather than discarded, because `setParameter` can be called from a
thread other than the audio one.

## Known limitations

- Grid events are quantised to the processing block, matching the reference
  plugins here. At a 64 to 512 frame block that is at most a few milliseconds,
  under the 8 ms default attack.
- The wavetable tables are synthesised with a fixed harmonic count rather than
  mip-mapped per pitch, so very high notes can alias slightly. Plinky has the
  same behaviour at the top of its range.
- The reverb is a four-comb tank with a damped, shimmer-modulated feedback path,
  not Plinky's full implementation. It is a reasonable tail, not an exact match.

## Testing

`tests/plank_core_tests.cpp` uses a hand-rolled `require()` rather than
`assert()`, matching `lifeform`. The Release build adds `-DNDEBUG`, which compiles
every `assert()` out and lets a suite pass while testing nothing; `require()`
cannot be compiled away, so no `-UNDEBUG` block is needed in `CMakeLists.txt`.

The suite covers the grid note mapping, the scale ladder including rotate and
stride, one-string-per-column allocation, latch behaviour, the filter actually
moving the filter, morph crossfading between two different waveforms, wavetable
band-limiting at the loop point, the Launchpad LED and MIDI channel contract,
panic, and determinism.
