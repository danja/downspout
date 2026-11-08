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

**Not usable yet:** the overlap-add reads each output sample at the moment it is written, so the
wet path does not resynthesise the input (a unity mask comes out 17.7 dB down and uncorrelated).
Mix 0 is dry and correct. See "Known bug" in `docs/design.md`.

See docs/design.md.