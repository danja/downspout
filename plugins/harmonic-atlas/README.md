# Harmonic Atlas

Harmonic Atlas is an original, autonomous transport-synced MIDI harmony
generator. It emits bounded chords without MIDI input and can optionally use
incoming notes as pitch-class roots.

**Gravity** (0-100%, default 0 = unchanged) pulls the progression towards functional
harmony. Each chord may be replaced, with probability up to 85%, by a stronger root chosen
by what came before: after V the next chord goes home, after IV it usually goes to V, after
I it moves to IV, V or vi, and any other chord drifts to I, IV or V. Cadence positions always
land on the tonic. The choice is a pure function of the chord number and seed, so loops and
offline renders agree. The idea comes from Subsequence's weighted chord graph with key
gravity (see `docs/subsequent.md`). The parameter is appended after the original ones so
saved projects keep their meaning.

**Voice-leading** above 50% now chooses each chord's inversion and octave placement to
move the voices as little as possible from the previous chord (sum of sorted voice
distances, with a penalty outside C2-C6). The previous chord is itself voiced the same way,
six chords back, so a chord depends only on its number and playback from any point, a loop or
an offline render gives the same notes. Below 100% the choice is seeded among voicings within
a window of the best, so 100% is strictest. Voicing spread still limits how many inversions are
tried (up to the setting plus one). At 50% or below the original fixed stacking is used. Note
that the default setting (75%) is above the threshold, so default output now voice-leads; before,
that range only dropped notes above 72 by an octave.

Its four movement families are tonal, modal, chromatic-mediant, and
neo-Riemannian-inspired. Seeded choices are derived from absolute harmonic
position, so playback, loop restart, and offline rendering are repeatable.
