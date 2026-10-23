---
title: Monochord
order: 224
bundle: monochord.vst3
kind: Instrument
category: instrument
role: Waveguide string
screenshot: /assets/plugins/monochord.png
capabilities: [MIDI input, audio output]
summary: An eight-voice waveguide string that can be plucked, bowed or struck, with a loop tuned from its own phase delay, damping, stiffness, a pickup comb and a small body.
---

## Opinion

Not yet auditioned in a host. Downspout had mallets (Moka), birds (Syrinx) and a GM palette (Pratt) but no plucked or bowed string, so this is the one for guitar-like
plucks, harp-ish arpeggios, hammered strings and drones. The panel shows what you are doing to the string: the shape you start it in and the harmonics that gives, so
moving the pluck or the pickup is as visible as it is audible. It is a redesign, not a copy of the Oxide Studio device that inspired it, whose string plays flat.

## Functionality

Each voice is a string modelled as a delay line closed into a loop: a damping low-pass, a fractional-delay all-pass and a four-section stiffness all-pass sit in the
loop. The loop is tuned from its own phase delay, so the note is in tune whatever the damping and stiffness (within 0.4 cents everywhere it can be measured).

### Exciters

- **Pluck**: the string starts as a triangle with its apex at **Position**; a low **Hardness** rounds it off for a softer sound.
- **Hammer**: a half-sine pulse centred at **Position**, wide when soft and narrow when hard.
- **Bow**: friction is applied for as long as the key is held, so the note sustains; **Bow pressure** changes the sound, and the level follows the key velocity.

A string plucked a fifth of the way along has no fifth harmonic, and the bar chart on the panel shows that. A **Pickup** at 1/n of the way along removes every n-th harmonic.

### Parameters

| Control | Range | Default |
|---|---|---|
| Exciter | Pluck / Bow / Hammer | Pluck |
| Position | 0.05-0.5 | 0.2 |
| Hardness | 0-1 | 0.6 |
| Bow pressure | 0-1 | 0.5 |
| Decay | 0.1-20 s | 3 s |
| Damping | 0-1 | 0.3 |
| Stiffness | 0-1 | 0.5 |
| Body size / level | 0.5-2 / 0-1 | 1 / 0.3 |
| Pickup | 0-0.5 (0 = off) | off |
| Release | 5-2000 ms | 250 ms |
| Master, Velocity, Bend range | | 0.6, 0.7, 2 st |

### Decay, damping and stiffness

**Decay** is the time for the fundamental to fall 60 dB (within 12 % when Damping is 0). **Damping** makes the higher partials die faster: at 0.7 partial 8 falls about twice as
fast as the fundamental. Damping is per pass through the loop, so it is stronger on high notes, as on a real string. **Stiffness** sharpens the upper partials, like a thick string or
a piano bass string, without changing the fundamental: on a low note, partial 6 is 3 cents sharp at 0.5, 20 at 0.7 and 62 at 0.9.

### Playing it

MIDI notes with velocity (**Velocity** sets how much it matters), pitch bend over **Bend range** (the loop is retuned as you bend), the sustain pedal (CC 64; without it, a released
string is damped over **Release**) and CC 120 and 123. Eight voices; a ninth note steals the quietest. Mono voices to a stereo output (both channels equal). About 6 % of a core for eight notes.

### Status

Core covered by tests (134 checks, also under ASan/UBSan): the loop's phase delay, pitch to within a cent across notes, damping, stiffness, sample rates and a bend; decay time; damping and stiffness
against measurement; the pluck's harmonic nulls, the hammer's brightness and the pickup comb against theory; the excitation shapes; the bow; voices, pedal, panic and velocity; sample-accurate events;
every control; stability to 20 Hz at 192 kHz. Not yet listened to or validated in a host. One string per voice: no sympathetic strings or coupling.
