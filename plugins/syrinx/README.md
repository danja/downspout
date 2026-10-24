# Syrinx

`syrinx.vst3` — avian syrinx voice. Two ways to play it:

- **MIDI**: each note plays a phrase of chirping syllables (Syllables, Chirp, Variation, Gap).
- **Audio in**: the input is tracked for pitch and loudness and steers the bird, so it works as
  an effect on speech or singing (Pitch Multiplier, Voice Range, Gate, Pitch Glide, Hiss to
  Turbulence, Bird Mix). Source = MIDI / Audio in / Both.

The voice is the Mindlin-Laje syrinx ODE with a closed-loop pitch lock, a tracheal formant comb
and ten species presets. The panel uses the Magneto look. Design, formulas and assumptions are in
[docs/design-notes.md](docs/design-notes.md); parameter and CC lists are in
[the product page](../../docs/pages/_products/syrinx.md).

Tests: `downspout_syrinx_core_tests` (sweep depth and speed, syllable counts, pitch lock, tracker
accuracy, audio-mode pitch following, gate, mix, status outputs, parameter-index stability).
