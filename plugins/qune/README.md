# Qune

Stereo pitch quantiser. It tracks the pitch of a voice or a single-note instrument and
moves it to the nearest note of a chosen scale, or to the MIDI notes you hold. Three
quality tiers trade latency against smoothness, formants can be kept or moved, and a glide
control chooses between hard-tuned steps and a slide.

Magneto look: header, accent-banded panels, sliders and steppers, a dropdown for the scale
list, and a scrolling pitch trace showing what was heard and where it was sent.

## Signal flow

```
in L/R ─┬─ mono sum ─ pitch tracker ─ target note ─ glide ─ shift ─┐
        └────────── shifter (Fast / Good / Best) ◄───────────────┘ ─ mix ─ level ─ out L/R
```

One pitch is tracked from the sum of the two channels and applied identically to both, so
the stereo image and level difference survive. Unvoiced sound (breath, consonants, silence)
is passed through unchanged. Polyphonic input is not supported: the tracker follows one
pitch.

## Controls

| Control | Meaning |
|---|---|
| Root, Scale | Key and one of 24 scales (see `docs/scales.md`; ordering is the canonical one used by plank, sprout and markov) |
| Take notes from | **Scale**: snap to the scale. **MIDI chord**: snap to the nearest held pitch class, in any octave. **MIDI note**: sing the last note played |
| When no key is held | **Bypass** leaves the voice alone, **Use scale** falls back to the scale |
| MIDI input | All channels, or one |
| Pitch bend range | Semitones for MIDI pitch bend (applied to MIDI targets) |
| MIDI out | Sends the target note as MIDI note-on/note-off while a pitch is corrected, one note at a time, legato. Channel and a fixed velocity are set alongside |
| Amount | 0 leaves the pitch alone, 100 % lands it on the note |
| Glide | 0 = off (notes step at once); longer times slide between notes and let some vibrato through |
| Stickiness | Cents a pitch must travel past the halfway point before the target moves to the next note |
| Transpose | Whole semitones added to the target. With Amount at 0 this is a plain transposer |
| Voicing | How noisy a sound can be and still count as pitched |
| Quality | Fast, Good or Best (below) |
| Pitch range | Bass 40-400 Hz, Voice 90-900, Lead 160-1600, Wide 40-1600. Narrower is cheaper and, in Good, faster |
| Formants | **Follow pitch** (resampler-style) or **Preserve** the spectral envelope. Good and Best only |
| Formant shift | Moves the envelope by up to +-12 semitones on top of the mode. Good and Best only |
| Mix, Level | Dry/corrected balance (dry is delayed to match) and output gain |

## Quality tiers

| Tier | Algorithm | Latency | Formants | Notes |
|---|---|---|---|---|
| Fast | Pitch-synchronous splice delay line | about 4 ms nominal; the read head sits anywhere from a fraction of a millisecond to one period plus 3 ms | follow pitch | Cheapest. Best for live monitoring. At ratio 1 it is a plain delay |
| Good | Time-domain PSOLA, fractional pitch marks, normalised overlap | 2.4 periods of the range's lowest note: Lead 16 ms, Voice 28 ms, Bass and Wide 61 ms (at 48 kHz) | preserved, or moved by Formant shift | Clean on voice and monophonic instruments. Big downward shifts (more than about 4 semitones) squeeze the window and sound rougher |
| Best | Phase vocoder, peak locking, cepstral formant correction | 2048 samples at up to 50 kHz, scaled up at higher rates, about 43 ms | preserved, or moved by Formant shift | Smoothest on breathy, noisy or slightly polyphonic sound. Highest latency |

The latency in use is shown in the header, reported to the host, and used to delay the dry
path. Changing Quality or Pitch range fades the output out for about 10 ms around the
change, because it changes the latency of both paths.

## MIDI

Notes in: a note is a target, never a voice, so nothing is generated. Pitch bend moves MIDI
targets. CC 64 (sustain) holds released notes. CC 120 and 123 clear held notes. There are
no other CC mappings; every control is a host parameter, so automate those.

Notes out: the whole-semitone target (including Transpose, excluding pitch bend) is sent as a
note-on when it is first found or changes, with the new note-on placed before the old
note-off so a mono synth plays legato. Note-off follows when the sound stops being pitched,
and when MIDI Out is turned off, the channel changes, or the plugin is reset. The message is
timed at detection, so it leads the delayed audio by the reported latency.

## Documented assumptions

- **State.** None. Every setting is a parameter, which DPF saves by symbol. Activation and
  sample-rate change clear the audio and MIDI state only, never the patch.
- **Transport.** Not used. The effect has no tempo behaviour.
- **MIDI.** One MIDI input. Held notes are runtime state, not saved. MIDI events are applied
  at their sample position by splitting the host block.
- **UI.** The panel shows live pitch, target, shift and latency through four output
  parameters, because DPF has no DSP-to-panel path for incoming MIDI. The trace samples
  those at about 33 Hz.
- **Block size.** Control updates run on a 32-sample grid that restarts at each host block
  edge, so output differs by rounding-scale amounts between host block sizes.

## Known limits

- One pitch at a time. Chords, dense mixes and heavily distorted sound confuse the tracker.
- The first 10-30 ms of a new note can be corrected using the previous note's analysis.
- A large jump in pitch must repeat for two hops (about 8 ms) before it is believed, which is
  what rejects octave errors; a very fast legitimate leap is delayed by that much.
- Fast at Mix below 100 % is a creative blend rather than an aligned one, since the wet
  delay is not constant.
- Wide range at 96 kHz is the heaviest case; see `docs/design.md` for measured load.

See `docs/design.md` for the algorithms, the measurements behind the numbers above, and
what is still untested.
