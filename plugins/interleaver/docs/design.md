# Interleaver design notes

Original plugin (no LV2 source). Portable core in `include/interleaver_engine.hpp` and `src/interleaver_engine.cpp`; DPF and UI in `src/dpf/`.

## Ports

Four audio inputs, two outputs. A is inputs 1/2; B is inputs 3/4, declared as a second bus (`kAudioPortIsSidechain`, as in glue) so hosts
route it separately; both are ordinary program audio. No MIDI, no transport use, no plugin state: every control is an automatable parameter and
DPF saves them by symbol. The last two parameters (Detected Pitch, Cycle Length) are read-only outputs for the panel.

## Latency (assumption)

The spectral path needs one full 2048-sample frame. The time path is delayed by the same 2048 samples so that switching mode never moves the
output, and the host sees one fixed latency (`setLatency(2048)`). Mode changes fade over 20 ms. A test measures both paths with an impulse.

## Time mode

A cycle phase `phi` advances by `1 / cycle` per sample. A occupies `[0, Balance)`, B the rest. `timeMask(phi)` is the fraction of B: a step, or
edges of width `Crossfade x min(Balance, 1 - Balance)` (linear or raised cosine) centred on both boundaries; the output is `A cos(m pi/2) + B sin(m pi/2)`
(equal power). Edge widths never exceed the shorter segment, so the three edge terms never overlap. The right channel reads the mask at
`phi + Split / 2`. Jitter draws a new cycle-length factor `1 +- Jitter/2` at each wrap. The cycle length follows its target through a one-pole
(about 500 samples), so tracking glides rather than jumps. The sample-rate limit of the "zipper" case is a 2-sample cycle.

"Interpolation" is exposed as the edge shape: Hard has none (the mask is quantised to sample boundaries, which aliases audibly), Linear and
Cosine interpolate the gain across the join.

## Pitch tracking

Mono sum of the chosen source, boxcar-decimated to about 12 kHz, normalised autocorrelation over 512 samples every 128 decimated samples, lags
50 Hz-1 kHz. It takes the *shortest* local maximum that reaches 90 % of the best (avoids octave-down errors), refines it parabolically, and
requires a correlation of 0.6. After 5 failed analyses it reports unvoiced and the free Slice is used. It is not locked to waveform phase: segment
lengths follow the period but splices happen at arbitrary points in the cycle.

## Spectral mode

2048-point FFT, hop 512, Hann analysis and synthesis (overlap-add sums to 1.5, normalised). Two real signals share a complex FFT (left real, right
imaginary), so a hop costs three FFTs. Bin `k` is taken from B when `frac((k + offset) / (2 width)) >= Balance`; the right channel's offset is
shifted by `Split x width`. Mag/Phase keeps one source's magnitude with the other's phase. Loudest compares magnitudes with A weighted by
`10^((Balance - 0.5) x 48 / 20)`. Only bins 0..N/2 are chosen; the mirror is the conjugate, so the output is real. Drift advances the offset by
`Drift x hop / sampleRate` bins per hop (kept in a double). The spectral path only runs while it is selected or fading out.

## Not done

Phase locking of splices to waveform zero crossings; pitch-synchronous spectral modes; frame-rate transient handling (smearing at 2048 is expected);
a live waveform or spectrum display (the UI receives only parameters, so the plot is the mask, not the audio).

## Verification status

Core tests only. Not listened to, not loaded in a host. The screenshot is of the standalone build.
