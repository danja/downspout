# Sieve

Spectral sieve: an STFT mask that keeps only partials near the notes of a chosen scale (or held MIDI notes), in every octave. Differs from resonance-garden: it keeps the source's own texture instead of ringing new resonators.

## Controls
- Root, Scale: key and scale
- Source: Scale or Held MIDI
- MIDI In: channel filter
- Width: cents half-width of each passband
- Floor: minimum gain (dB)
- Tilt: odd/even harmonic tilt
- Mix, Level: dry/wet balance and output gain

Magneto look. Latency ~1024 samples.

See docs/design.md.