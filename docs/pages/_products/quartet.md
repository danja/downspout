---
title: Quartet
order: 223
bundle: quartet.vst3
kind: Instrument
category: instrument
role: FM synthesizer
screenshot: /assets/plugins/quartet.png
capabilities: [MIDI input, audio output]
summary: An eight-voice four-operator FM synthesizer with eleven algorithms, band-limited sine, saw and square operators, per-operator envelopes, feedback, optional 2x oversampling, a pitch envelope and a filter.
---

## Opinion

Not yet auditioned in a host. Downspout had no FM synthesizer, so this covers electric pianos, bells, plucks, metallic basses and the classic
digital pad. The panel draws the algorithm as a diagram, so you can see which operator is pushing which before you touch a slider. It is a
redesign, not a copy of the Oxide Studio device that inspired it, whose default modulation index is about 19 radians and whose "fine" tuning moves
a frequency by up to an octave.

## Functionality

Each of the four operators (A to D) is a sine, a saw or a square, with a frequency that is a multiple of the note (**Ratio**, with **Fine** in cents) or
a **Fixed** value, a level and its own ADSR envelope. The **algorithm** decides how they connect: in algorithm 1 D modulates C, which modulates B, which
modulates A, and only A is heard; in algorithm 11 all four are heard and none modulates. A modulator's **Level** is how far it pushes its target's phase,
and **FM Depth** sets the scale: the modulation index in radians is 2 pi x depth x level, so a depth of 1 and a level of 0.16 is an index of 1. Operator D
can **feed back** into itself for a raspier tone.

### Algorithms

(`>` means modulates.) 1 D>C>B>A; 2 (C+D)>B>A; 3 (B + D>C)>A; 4 D>(B+C)>A; 5 (B>A) + (D>C); 6 (B+C+D)>A; 7 A + B + (D>C); 8 A + (D>C>B); 9 A + ((C+D)>B);
10 D>(A+B+C); 11 A+B+C+D. Carriers are averaged, so more carriers do not mean more level.

### Parameters

| Group | Controls |
|---|---|
| Per operator | Waveform, Ratio or Fixed, Ratio 0.25-32, Fine +-50 ct, Fixed Hz, Level, Attack, Decay, Sustain, Release |
| FM | Algorithm, Feedback, FM Depth 0-4 cycles, Quality (1x or 2x oversampling) |
| Pitch envelope | On, Amount +-48 st, ADSR |
| Filter | Type (LP 12, LP 24, HP 12, BP, Notch), Cutoff, Resonance, Key track |
| Performance | Master, Velocity, Bend range |

### Clean at the top

The saw and square operators are band-limited (inharmonic power at most -77 dB at every pitch). FM itself can still alias: high-index FM at a high pitch has
sidebands far beyond Nyquist. The **Quality** switch runs the voice at twice the sample rate and decimates with a 63-tap half-band filter (flat to 18 kHz within 0.001 dB,
-89 dB stopband). On a deliberately extreme three-stage patch this lowers the aliasing from -17 to -53 dB at note 72, from -8 to -26 dB at note 84, and from -2 to -8 dB
at note 96. Keep the depth and levels moderate for the top octaves.

### Playing it

MIDI notes with velocity (**Velocity** sets how much it matters), pitch bend over **Bend range**, the sustain pedal (CC 64) and CC 120 and 123. Eight voices; a ninth note
steals the quietest. Mono voices summed to a stereo output (both channels identical). About 5 % of a core for eight notes at 1x and 13 % at 2x.

### Status

Core covered by tests (493 checks, also under ASan/UBSan): the waveform tables and their band limits, the decimator's response, the algorithm table and every declared routing,
the FM sidebands against Bessel functions (exact to four digits), pitch to half a cent, feedback, aliasing with and without oversampling, DC, the pitch envelope, the filter
against its model, envelope shapes, voices, pedal, panic and velocity, sample-accurate events, every one of the 57 controls, and stability at 44.1 to 192 kHz. Not yet listened
to or validated in a host. No user-drawn waveform, key or velocity scaling per operator, 4x oversampling, or mono mode.
