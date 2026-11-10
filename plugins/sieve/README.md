# Sieve

Spectral sieve: an STFT mask that keeps only partials near the notes of a chosen scale (or held MIDI notes), in every octave. Differs from resonance-garden: it keeps the source's own texture instead of ringing new resonators.

## Controls
- Root, Scale: key and scale, used by **Scale** and as the fallback when **Held MIDI** has no notes down
- Source: Scale or Held MIDI
- MIDI In: channel filter for Held MIDI (all, or 1-16)
- Width: cents half-width of each passband
- Floor: minimum gain (dB)
- Tilt: odd/even harmonic tilt
- Mix, Level: dry/wet balance and output gain

Magneto look. Latency is exactly 1024 samples (about 21 ms at 48 kHz), reported to the host; the dry
path is delayed to match.

**Floor** at 0 dB makes the mask unity, so the wet path is a pure delay. A **Width** narrower than the
FFT bin spacing (about 183 cents at 440 Hz) rejects most of a sine's main lobe, which is why the
default 50-cent Major setting sounds like a narrow comb rather than a transparent filter. Widen it
to keep more of the source.

The wet path resynthesises correctly: unity-mask round trip, a single-tap impulse response, and
block-size invariance are covered by `tests/sieve_core_tests.cpp`, along with Width, Floor, Tilt,
Mix, Level, Root/Scale, and Held MIDI channel filtering. See `docs/design.md`.
