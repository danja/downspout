# Primefold

`primefold.vst3` — prime-harmonic feedback pitch shifter (stereo audio effect).

Dry input is pitch-shifted directly to the prime ratios 2x, 3x and 5x.
Every non-prime harmonic is supplied by recirculation through the same three
shifters: 4x is 2x fed back into 2x, 6x is 2x into 3x (or 3x into 2x), 8x is
three passes through 2x, 9x is two passes through 3x, 10x is 2x/5x, and so on.
There are no dedicated shifters for composite ratios. Full design — shifter
choice, feedback network, gain bounds, latency rules — is in
[docs/design.md](docs/design.md).

## Using it

Insert on a stereo track. Start from the default (2x 70%, 3x 50%, 5x 40%,
Feedback 35%, Mix 60%) and raise **Feedback** to bloom composites: 4x and 6x
arrive first, then 8x/9x/10x/12x/15x as the loop recirculates. **Damp**
darkens each pass. **Grain** sets the nominal reciprocation depth
(512/1024/2048 samples); the reported feedforward latency is the worst-case
voice delay (grain + 104), and the dry path carries the nominal average
(grain/2) so dry/wet stays aligned on average.

Latency contract: the host sees grain + 104 samples via `setLatency`. Every
feedback cycle additionally carries an explicit 256-sample delay plus the
voice's own minimum delay, and that in-loop latency is never compensated —
composites are late by roughly `k x (grain + 256)` per pass, which reads as bloom.

**MIDI control.** CC 1 feedback, CC 2 mix, CC 7 output, written through to
host parameters so automation and panel agree. The CLIP lamp is the
processor's live soft-clip state.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_PRIMEFOLD=ON
cmake --build build --target downspout_primefold_core_tests primefold-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_primefold_core_tests --output-on-failure
```

Core tests cover parameter clamping, silence stability, per-voice shift ratio
(2x/3x/5x within 6%), feedback-supplied composites, 10 s max-feedback
boundedness, block-size invariance, state round trip, and latency reporting.

## Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER is pending.
