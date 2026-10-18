---
title: Qune
order: 214
bundle: qune.vst3
kind: Audio effect
category: processor
role: Pitch quantiser
screenshot: /assets/plugins/qune.png
capabilities: [audio input, audio output, MIDI note input, reported latency]
summary: Tracks the pitch of a voice or single-note instrument and moves it to the nearest note of one of 24 scales, or to MIDI notes you hold. Three quality tiers, glide, formant preserve and shift.
---

## Opinion

A tuner that can be heard doing its job or kept out of earshot. With **Glide** off and **Amount**
at 100 % it is the hard, stepped correction everyone knows; with a glide of a couple of
hundred milliseconds and Amount at 60 % it nudges a singer toward the key and leaves the
vibrato. Switching **Take notes from** to MIDI turns it into something else: hold a chord
and the voice lands on the nearest chord tone, or play a melody and the voice sings it.

The quality tiers are worth trying on the same take. **Fast** is a few milliseconds and
sounds like a resampler; **Good** keeps formants and is clean on a single voice; **Best** is
smoother on breath and noise and costs 43 ms. The pitch trace on the panel shows what was
heard and where it was sent, which is the quickest way to tell whether **Stickiness** and
**Voicing** are set for your source.

It follows one pitch. Feed it a voice, a bass or a lead line, not a chord.

## Functionality

A normalised-square-difference pitch tracker (decimated search, full-rate refinement,
octave-error checks) reads the sum of the two channels every 4 ms. The target note comes from
the scale or from held MIDI notes; **Amount**, **Glide**, **Stickiness** and **Transpose**
turn the difference into a shift, and one of three shifters applies it to both channels:

| Tier | Algorithm | Latency (48 kHz) | Formants |
|------|-----------|------------------|----------|
| Fast | Pitch-synchronous splice delay line | about 4 ms | follow pitch |
| Good | Time-domain PSOLA, fractional pitch marks | 16 ms (Lead) / 28 ms (Voice) / 61 ms (Bass, Wide) | preserved, or moved |
| Best | Peak-locked phase vocoder, cepstral envelope | about 43 ms | preserved, or moved |

Unvoiced sound passes through unchanged. The dry path is delayed to match, and the latency is
reported to the host and shown in the header.

**MIDI.** *MIDI chord* snaps to the nearest held pitch class in any octave; *MIDI note* sings
the last note played. Pitch bend (range set on the panel) moves the target, CC 64 holds
released notes, CC 120/123 clear them. **When no key is held** chooses between leaving the
voice alone and falling back to the scale. No other CCs are mapped; automate the host
parameters.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Root | C-B | C | |
| Scale | 24 scales | Major | Canonical ordering, chromatic first |
| Target | Scale, MIDI chord, MIDI note | Scale | |
| No Notes | Bypass, Use scale | Use scale | MIDI modes only |
| MIDI In | All, 1-16 | All | |
| Bend Range | 0-24 st | 2 | |
| Amount | 0-100 % | 100 % | |
| Glide | 0-1000 ms | 20 ms | 0 = off |
| Stickiness | 0-50 cents | 15 | |
| Transpose | -24 to +24 st | 0 | Whole semitones |
| Quality | Fast, Good, Best | Good | Changing it ducks the output for about 10 ms |
| Range | Bass, Voice, Lead, Wide | Voice | 40-400, 90-900, 160-1600, 40-1600 Hz |
| Voicing | 0-100 % | 50 % | Higher accepts breathier sound as pitched |
| Formant | Follow pitch, Preserve | Preserve | Good and Best |
| Formant Shift | -12 to +12 st | 0 | Good and Best |
| Mix | 0-100 % | 100 % | |
| Level | -24 to +12 dB | 0 | |

### Status

Core DSP, 190 000 deterministic checks, the VST3 target and the catalogue screenshot are
complete. It has not been auditioned on real voices and has not been run in a host, so
latency reporting, quality switching under automation and MIDI input are unverified outside
the test rig.
