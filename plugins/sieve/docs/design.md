# Sieve design notes

STFT mask (sqrt-Hann, 1024-pt, hop 256). Per bin, compute nearest scale note (or held MIDI note) in cents; gain = gaussian over width, floored at floor dB, with odd/even tilt. Overlap-add resynthesis. Latency ~1024 samples.

Controls as in README. Output level applied.

## Known bug (2026-11-08): the wet path does not resynthesise

The overlap-add is misaligned. `processBlock` accumulates the inverse-transformed frame at
`base = frameCounter % kFftSize` and then reads the output at
`outIdx = (frameCounter - kFftSize) % kFftSize`, which is the *same* index. So each sample is
read at the moment it is written, at `window[0] = 0`, while the other three frames that should
overlap it arrive 256/512/768 samples later and are read 1 024 samples after that.

Measured with **Floor** 0 dB, where every bin's gain is exactly 1 and the engine should be a
pure delay of `latency` samples:

- white noise: output 17.7 dB below the input, best-lag correlation 0.0085 (lags 0-2 048);
- a 440 Hz sine with Root C / Major: -43 dB, though it sits on a scale note and a 50-cent
  **Width** should pass it at about 0 dB (it is cancelled, not filtered);
- impulse: taps at 255/511/767/1023/1279 with a peak of 0.64 instead of one tap of 2.0.

Mix 0 is bit-exact dry, which is why the pass-through *looks* fine at the default if nothing is
listened to. Dead code from an earlier attempt is left at `src/sieve_engine.cpp:192-196`
("align? use simple").

Fix: keep an output read pointer that is a full `kFftSize` behind the write pointer, or read the
sample that all four overlapping frames have already contributed to (i.e. read at
`frameCounter + hop - kFftSize`), and normalise by the window sum so a unity mask is 0 dB. Then
add the tests listed in `MISTAKES.md`: unity-mask round trip = input delayed by `latency`,
within 0.1 dB and correlation > 0.999; on-scale sine passes; between-note sine attenuated by
**Floor**.

## Wrapper identity (fixed 2026-11-08)

`src/dpf/DistrhoPluginInfo.h` had been copied from Mouth: `DISTRHO_PLUGIN_NAME "Mouth"`, Mouth's
URI, CLAP id and `DISTRHO_PLUGIN_UNIQUE_ID Mout`. A DAW therefore listed Sieve as a second Mouth
(clashing with the real one). Now `"Sieve"`, `.../plugins/sieve`, `it.hyperdata.downspout.sieve`,
`Siev`.
