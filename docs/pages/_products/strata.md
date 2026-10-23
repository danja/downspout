---
title: Strata
order: 222
bundle: strata.vst3
kind: Instrument
category: instrument
role: Wavetable synthesizer
screenshot: /assets/plugins/strata.png
capabilities: [MIDI input, audio output]
summary: An eight-voice wavetable synthesizer with band-limited tables, Bend, Sync and Fold warps, a sine sub, unison, a multimode filter, two envelopes and an LFO.
---

## Opinion

Not yet auditioned in a host. Downspout had no general-purpose synthesizer, so this is the one to reach for for pads, leads and
basses you want to sweep: scroll Position through a table while an envelope or the LFO moves it, warp the cycle for harder timbres, and
thicken it with unison. It is a redesign, not a copy of the Oxide Studio device that inspired it, whose tables alias at high notes and
whose unison cancels the fundamental when Amount is 0.

## Functionality

Two oscillators each read one of five table families (Basic: sine to triangle to saw to square; Harmonics; Formant: a vowel sweep;
Digital: FM and fold; Vintage: a saturated saw). Each family has eight frames and **Position** scrolls through them with a crossfade.
The tables are stored in 21 band-limited versions and the oscillator reads the one that stays under Nyquist for the pitch it is
playing, so a high note stays clean: a square wave measures -77 to -89 dB away from its harmonics at every pitch, against -12 to -25 dB
for the same table read naively.

### Warp

**Bend** moves the middle of the cycle, **Sync** restarts it up to eight times faster (the classic hard-sync sound), and **Fold** folds the
waveform back on itself (antialiased). Warping a band-limited table makes content beyond Nyquist that cannot be removed afterwards; at
note 60 and amount 0.7 the aliasing measures -42.6 dB (Bend), -38.5 dB (Sync) and -33.1 dB (Fold). The plot above the Table control shows
the waveform through the warp.

### Parameters

| Group | Controls |
|---|---|
| Oscillator 1 and 2 | Table, Position, Warp, Warp amount, Transpose +-24 st, Fine +-50 ct, Level |
| Sub | Level, Octave (-1 or -2) |
| Unison | Mode (Classic, Shimmer, Noise), Voices 1-8, Amount |
| Filter | Type (LP 12, LP 24, HP 12, BP, Notch), Cutoff, Resonance, Key track, Envelope to cutoff, Envelope to position |
| Envelopes | Attack, Decay, Sustain, Release for the amplitude and the filter envelope |
| LFO | Rate, Shape (Sine, Triangle, Saw, Square, S&H), to Position, to Pitch, to Cutoff |
| Performance | Master, Velocity, Bend range, Play mode (Poly, Mono, Legato), Glide |

### Unison

At **Amount** 0 all unison voices play in step, so the level and the fundamental stay the same however many voices you ask for. Raising
Amount spreads their detune (up to +-50 cents in Classic) and their stereo position. Shimmer lifts odd voices by up to an octave; Noise
adds a slow random drift to each voice.

### Playing it

MIDI notes with velocity (the **Velocity** control sets how much it matters), pitch bend over **Bend range**, the sustain pedal (CC 64), and CC
120 and 123 for all sound off and all notes off. Eight voices; a ninth note steals the quietest. Every control is an automatable parameter.

### CPU

About 2 % of a core for one note and 9 % for eight at the defaults; 28 % for eight notes with 8 unison voices on both oscillators and the 24 dB
filter. The heaviest setting (Fold on one oscillator and Sync on the other with that much unison) reaches about 90 % of a core.

### Status

Core covered by tests (300 checks, also under ASan/UBSan): the tables' definitions, normalisation, band limits and reads; pitch to half a cent; aliasing
against a naive read; warps and their DC; unison level and width; the filter against its model; envelope and LFO shapes; voices, pedal, panic and velocity;
sample-accurate events; every control; stability at 44.1 to 192 kHz; the parameter table against the core. Not yet listened to or validated in a host. No
oversampling, table import, modulation matrix, a per-voice rather than global glide.
