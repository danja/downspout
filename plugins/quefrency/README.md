# Quefrency

`quefrency.vst3` — cepstral formant and harmonic shifter (stereo audio effect).

Each frame is split through the cepstrum into a formant envelope and a
harmonic fine structure; the two are shifted and reshaped independently and
multiplied back together. Ported from `~/github/jigdaw/plugins/quefrency`
(Rust/WebAssembly); parameter order, ranges, defaults and the CC 70–80 map
are unchanged. Porting decisions — above all the timing/latency handling the
web version flags as broken — are in [docs/design.md](docs/design.md).

## Using it

Insert on a stereo track. **Formant shift** moves resonances without moving
pitch; **Pitch shift** (plus **Fine**) moves every partial by the ratio while
the envelope stays; **Freq shift** offsets every partial by the same Hz
(inharmonic). **Harmonic depth** below 100 breathes, above buzzes;
**Formant depth** below 100 flattens the vowels, above exaggerates them.
**Lifter** is the cepstral split point: keep it below the shortest pitch
period or harmonics leak into the envelope and move with it. **Estimator**
switches the envelope climb (true envelope holds formants under shifting
better, at ~3x the CPU).

**Latency.** 2047 samples at 50 kHz and below, 4095 above — reported to the
host, which compensates. Shown live at the top of the panel.

**MIDI control.** CC 70–80 drive the eleven parameters in order, on any
channel; 64 selects the default. A CC holds until the host moves that same
control (last-wins); the panel keeps showing the host value.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_QUEFRENCY=ON
cmake --build build --target downspout_quefrency_core_tests quefrency-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_quefrency_core_tests --output-on-failure
```

Core tests bind the source suite's claims: impulse latency at three rates,
neutral noise reconstruction below −90 dB, dry/wet alignment, gain, silence
and extreme-setting finiteness, octave separation, formant/pitch separation,
the CC map, state round trip, and identical output across block sizes.

## Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER is pending.
