# Sieve design notes

STFT mask (sqrt-Hann, 1024-pt, hop 256). Per bin, compute nearest scale note (or held MIDI note) in cents; gain = gaussian over width, floored at floor dB, with odd/even tilt. Overlap-add resynthesis. Latency is exactly 1024 samples.

Controls as in README. Output level applied.

## Signal path and latency

Each channel has a 1024-sample input ring and a 2048-sample overlap-add ring. `outPos` is one full
frame behind the write region, so a sample is read only after all four overlapping frames have
contributed to it. The periodic sqrt-Hann analysis and synthesis windows make those four
contributions sum to 2, so `kOverlapGain = 2 * kHop / kFftSize = 0.5` passes a unity mask at 0 dB.

Latency is exactly `kFftSize` (1024 samples, about 21 ms at 48 kHz) and is reported to the host. The
dry tap is the input from one frame ago, so **Mix** blends dry and wet in phase. **Mix** 0 is that
delayed dry signal, not an undelayed bypass.

## Mask

Every bin is converted to a fractional MIDI note. The nearest allowed note is found within an octave
either side: a scale degree for **Scale**, or one of the notes held on the selected **MIDI In**
channel. The gain is a Gaussian in cents with the **Width** as its standard deviation, then floored
at **Floor**. **Tilt** scales odd bins by `1 + 0.3 * Tilt` and even bins by `1 - 0.3 * Tilt`. DC is
passed at unity.

The DPF wrapper applies note-on/note-off with `handleMidiNote()`, which filters by channel
(0 = all, 1-16) and treats note-on velocity 0 as note-off. When **Held MIDI** has no notes down, the
mask falls back to **Root** and **Scale** rather than going silent.

A **Width** of 50 cents is narrower than one FFT bin at low frequencies (about 183 cents at 440 Hz),
so a sine's leakage bins fall outside the passband even when its centre note is in the scale. That
is expected STFT behaviour: the filter is a narrow comb, not a transparent gate. Widening the band
keeps more of the source.

## Validation (2026-11-10)

Measured with the core engine at 48 kHz, 16 384 samples, 128-sample blocks, analysis after the
latency:

- unity mask (**Floor** 0 dB, **Mix** 100 %, **Level** 0 dB): white noise within 0.01 dB of the
  input, correlation 1.000000 at lag 1024, and the best lag over 0-2048 is 1024;
- impulse at input sample 100: a single output tap at 1124 of 1.0, every other tap below 1e-4;
- block sizes 1, 7, 64, 255, 256, 512, 1024 and 4096: sample-identical to the 128-sample render;
- chromatic **Width** 50 passes 440 Hz at -0.83 dB; Major passes the bin-centred 984.375 Hz tone at
  -0.93 dB and rejects 440 Hz at -24.45 dB;
- **Floor** with chromatic **Width** 1 at 450 Hz: -20.00 dB, -12.00 dB and -6.00 dB, matching the
  setting;
- **Mix** 0 equals the input delayed by 1024 samples to within 1e-6;
- **Level** -6 dB and +6 dB shift the output by exactly that amount; **Tilt** raises odd bins and
  lowers even ones; **Root** C vs C# changes a B5 pass from -0.9 dB to -10.1 dB; a held G#4 passes
  421.875 Hz where the C-major fallback rejects it, and the channel filter ignores the wrong channel.

`plugins/sieve/tests/sieve_core_tests.cpp` keeps these as regular tests, including table pins for
the 24 canonical scales of `docs/scales.md`.

## Wrapper identity (fixed 2026-11-08)

`src/dpf/DistrhoPluginInfo.h` had been copied from Mouth: `DISTRHO_PLUGIN_NAME "Mouth"`, Mouth's
URI, CLAP id and `DISTRHO_PLUGIN_UNIQUE_ID Mout`. A DAW therefore listed Sieve as a second Mouth
(clashing with the real one). Now `"Sieve"`, `.../plugins/sieve`, `it.hyperdata.downspout.sieve`,
`Siev`.
