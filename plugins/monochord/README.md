# Monochord

Eight-voice waveguide string: plucked, bowed or hammered. A redesign inspired by the Tension device in
`~/github/oxide-studio` (`oxide-dsp`, MIT/Apache-2.0), not a port: see `docs/design.md`.

Each voice is a delay line closed into a loop with a one-pole damping low-pass, a first-order all-pass for the fractional delay and a
cascade of four all-passes for stiffness. The loop is **tuned from its own phase delay**: the all-pass fraction is chosen so that the
whole loop has exactly one period of delay at the note, whatever the damping and stiffness, so every note is in tune. A pickup comb, two body
resonances and a damper on note-off complete it. The panel plots the string's initial shape with the pluck and pickup positions, and the
harmonics that follow, computed from the same shape and comb.

Instrument: MIDI in (notes with velocity, per-channel pitch bend over Bend range, sustain pedal CC 64, CC 120 and 123), mono voices to a stereo output (both channels
equal), no audio input and no transport use. All 14 controls are ordinary automatable parameters; there is no plugin state.

| Control | Range | Default | Notes |
|---|---|---|---|
| Exciter | Pluck / Bow / Hammer | Pluck | |
| Position | 0.05-0.5 | 0.2 | Where along the string it is plucked, struck or bowed |
| Hardness | 0-1 | 0.6 | A soft pluck or wide hammer at 0; no effect on the bow |
| Bow pressure | 0-1 | 0.5 | Bow only |
| Decay | 0.1-20 s (log) | 3 s | Time for the fundamental to fall 60 dB (with Damping 0; damping removes more on high notes) |
| Damping | 0-1 | 0.3 | How much faster the high partials die |
| Stiffness | 0-1 | 0.5 | Sharpens the upper partials: on a low note, partial 6 is +3 cents at 0.5, +20 at 0.7, +62 at 0.9 |
| Body size / level | 0.5-2 (log) / 0-1 | 1 / 0.3 | Two resonances at 120 and 280 Hz times the size |
| Pickup | 0-0.5 | 0 (off) | Comb along the string: a pickup at 1/n removes every n-th harmonic |
| Release | 5-2000 ms (log) | 250 ms | The damper after note-off |
| Master, Velocity, Bend range | | 0.6, 0.7, 2 st | |

A plucked note with Damping 0 falls 60 dB in the Decay time (within 12 %). The bow oscillates at every pressure and velocity, and its level
follows the velocity. CPU (a busy machine, so an upper bound, 48 kHz, -O2): about 0.8 % of a core for one note and 6 % for eight.
