# quefrency — port design notes

Source: `~/github/jigdaw/plugins/quefrency` (Rust `no_std` WebAssembly,
`jig:Abi2`): a stereo STFT effect that splits each frame through the cepstrum
into a formant envelope and a harmonic fine structure, transforms the two
independently, and multiplies them back together. The source design is in
`~/github/jigdaw/docs/plugins/quefrency-design.md`; this document records only
what the VST3 port changes and why, with particular attention to the
timing/latency problems flagged against the web version.

## What is ported faithfully

The DSP core is a line-for-line port of `src/lib.rs`: radix-2 FFT with
`1/n`-scaled inverse, periodic square-root Hann analysis/synthesis windows,
log-magnitude cepstrum with lifter split at `Nc`, optional 4-iteration true
envelope, mean-referenced envelope warp (shift/depth/tilt), Laroche–Dolson
peak-region pitch/frequency shift with accumulated phase rotation, harmonic
depth scaling, every change applied as a gain on `X` (neutral = exact
reconstruction), overlap-add at 75%, dry delay line, mix and output gain.

Parameter order, ranges, defaults, units and the CC map (70–80, centre-detent
64 → default) are taken from `profile.json` unchanged, so behaviour stays
traceable to the source. `quefrency_core_tests.cpp` re-implements the
binding claims of `tests/dsp/quefrency.test.js`: neutral reconstruction to
−90 dB, impulse latency binding, dry/wet alignment, silence/finiteness,
formant-vs-pitch separation, CC mapping, block-size invariance.

## What the flagged timing/latency problems were, and what changes

1. **The module ABI carries no latency, so no native host is told it.**
   The profile says this outright as a `trn:caution`. The VST3 port enables
   `DISTRHO_PLUGIN_WANT_LATENCY` and calls `Plugin::setLatency(N − 1)` —
   2047 at 50 kHz and below, 4095 above — on construction, `activate()` and
   `sampleRateChanged()`. No parameter affects `N`, so latency is constant
   per sample rate and never changes during playback (per the source's own
   latency §2 rule: pad to worst case, don't change mid-stream).
2. **Frame size follows the sample rate** (2048 ≤ 50 kHz, 4096 above). A
   rate change therefore changes latency; the wrapper re-runs `activate()`
   (clearing delay memory, as `jig_init` does) and re-reports. The core
   exposes `currentLatencySamples()` and the UI shows it as a read-only
   status parameter, so the panel stays honest.
3. **The web version runs in fixed 128-frame quanta; VST3 blocks are
   arbitrary.** The core loops sample-by-sample and triggers frame
   processing when `filled == hop`, so any host block size — including
   non-multiples of the hop — produces identical output (tested). The
   `N − 1` figure (not `N − R`) is preserved: each output sample is read
   immediately after any frame processed on that sample, and the dry line is
   exactly `N − 1`, so mix < 1 does not comb.
4. **MIDI CC has no per-event sample offset in DPF** (`MidiEvent` carries no
   frame). The source applies each event at its own frame within the block;
   the port applies a block's CCs at the block start. Spectral parameters
   already take effect only at the next hop, so the practical difference is
   confined to mix/output (per-sample in source, per-block here) — inside
   one host buffer, inaudible, and documented here rather than hidden.
5. **Last-wins CC semantics are kept.** A CC writes an internal override;
   the next host write to the same parameter clears it. The panel keeps
   showing the host value (DPF has no DSP→panel push), matching the source
   profile's caution that a controller's value is neither shown nor saved.

## Control rate versus audio rate

As in the source: mix and output gain act per sample; the nine spectral
parameters are read once per analysis frame (hop = N/4). There is no
additional smoothing — the source has none, and stepwise-per-frame is the
documented behaviour. FFT tables, windows and the tilt table are built in
`activate()`; nothing allocates after it.

## Cost

Worst case per channel per frame: forward + inverse + 2 lifter FFTs, plus 8
more with the true envelope — 12 × 4096-point FFTs at 96 kHz, both channels.
This is the source's own budget (measured ~13% in node at 48 kHz); desktop
hosts absorb it, but true-envelope + full pitch shift at 96 kHz is the
heaviest corner and is noted on the panel as such. No quality switch is
added: that would be a new feature, not a port.
