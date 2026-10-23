# Strata

Eight-voice wavetable synthesizer. A redesign inspired by the Wavetable device in
`~/github/oxide-studio` (`oxide-dsp`, MIT/Apache-2.0), not a port: the source's tables are not
band-limited and alias heavily, and its unison cancels the fundamental at Amount 0 (see
`docs/design.md`).

Two oscillators read five table families (Basic, Harmonics, Formant, Digital, Vintage), each of eight
frames that **Position** scrolls through. Every frame is stored as 21 band-limited copies, so a note at any
pitch reads a copy that stays under Nyquist. **Warp** bends the cycle (Bend), restarts it faster
(Sync), or folds it (Fold, antialiased). Add a sine sub, unison, one resonant filter (LP 12, LP 24, HP 12, BP,
Notch), an amplitude and a filter envelope, and an LFO. The panel plots each oscillator's waveform as you set it.

Instrument: MIDI in (notes with velocity, pitch bend, sustain pedal CC 64, CC 120 and 123), stereo out, no audio
input and no transport use. All 41 controls are ordinary automatable parameters (`osc1_table` ... `bend_range`);
there is no plugin state.

| Group | Controls |
|---|---|
| Oscillator 1 and 2 | Table (5), Position 0-1, Warp (None / Bend / Sync / Fold), Warp amount, Transpose +-24 st, Fine +-50 ct, Level |
| Sub | Level, Octave (-1 or -2) |
| Unison | Mode (Classic / Shimmer / Noise), Voices 1-8, Amount |
| Filter | Type, Cutoff 20 Hz-20 kHz, Resonance, Key track, Envelope to cutoff, Envelope to position |
| Envelopes | Attack, Decay, Sustain, Release for the amplitude and the filter envelope |
| LFO | Rate 0.05-20 Hz, Shape (Sine / Triangle / Saw / Square / S&H), to Position, to Pitch, to Cutoff |
| Performance | Master, Velocity (0 = every note at full level), Bend range 0-24 st |

CPU (a busy machine, so an upper bound, 48 kHz, -O2): about 2 % of a core for one note and 9 % for eight notes at the
defaults; 28 % with eight notes at 8 unison voices on both oscillators and the 24 dB filter; up to about 90 % in that
worst case with Fold on one oscillator and Sync on the other. Warp aliasing is measured in `docs/design.md`.
