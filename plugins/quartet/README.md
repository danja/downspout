# Quartet

Eight-voice four-operator FM synthesizer. A redesign inspired by the Operator device in
`~/github/oxide-studio` (`oxide-dsp`, MIT/Apache-2.0), not a port: see `docs/design.md` for what is different and why.

Four operators A-D, each a sine, saw or square (the last two band-limited), with its own envelope and a frequency that is a
ratio of the note or a fixed value. Eleven algorithms decide which operators modulate which and which are heard; the panel
draws the routing from the same table the audio uses. Operator D can feed back into itself. **FM Depth** sets how far a
full-level modulator moves its target's phase, in cycles (the index in radians is 2 pi x depth x level). A pitch envelope, one
multimode filter, and optional 2x oversampling with a half-band decimator for cleaner high-index FM.

Instrument: MIDI in (notes with velocity, pitch bend, sustain pedal CC 64, CC 120 and 123), mono voices summed to a stereo
output (both channels identical), no audio input, no transport use. All 57 controls are ordinary automatable parameters
(`opa_wave` ... `quality`); there is no plugin state.

| Group | Controls |
|---|---|
| Per operator (x4) | Waveform, Ratio / Fixed, Ratio 0.25-32, Fine +-50 ct, Fixed Hz 10-20000, Level, Attack, Decay, Sustain, Release |
| FM | Algorithm 1-11, Feedback (D into itself), FM Depth 0-4 cycles, Quality (1x / 2x) |
| Pitch envelope | On, Amount +-48 st, Attack, Decay, Sustain, Release |
| Filter | Type (LP 12, LP 24, HP 12, BP, Notch), Cutoff, Resonance, Key track |
| Performance | Master, Velocity, Bend range |

Algorithms (letters are operators, `>` means modulates): 1 D>C>B>A; 2 (C+D)>B>A; 3 (B + D>C)>A; 4 D>(B+C)>A; 5 (B>A) + (D>C);
6 (B+C+D)>A; 7 A + B + (D>C); 8 A + (D>C>B); 9 A + ((C+D)>B); 10 D>(A+B+C); 11 A+B+C+D. Carriers are averaged, so the output
level does not rise with their number.

CPU (a busy machine, so an upper bound, 48 kHz, -O2): 1 % of a core for one note and 5 % for eight at 1x; 2 % and 13 % at 2x.
