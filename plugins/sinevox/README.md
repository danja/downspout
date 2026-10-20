# Sinevox

Formant tracker with two voices: a **vocoder** and a **sine-wave-speech generator**.

## Routing

| Port | Purpose |
|---|---|
| in 1/2 | **Speech** — summed to mono and analysed for formants |
| in 3/4 | **Carrier** — filtered by the formant bank (vocoder mode only) |
| out 1/2 | Result |
| MIDI in | CC and note control |
| MIDI out | Tracked formants as CCs or notes (off by default) |

This is the same slot layout as Voxmod (analysed signal on 1/2, coloured signal on
3/4), but Sinevox calls them speech and carrier.

## What it does

Every 5 ms an LPC analysis of the last 25 ms of speech finds up to eight resonances.
Each gets a **centre frequency**, a **Q** (frequency over bandwidth) and an
**amplitude** (the level of that resonance, with pre-emphasis undone). Tracks keep
their identity from frame to frame, so a formant glides rather than jumping slots.

- **Vocoder** — each track drives a band-pass filter on the carrier at the track's
  frequency and Q, scaled by its amplitude. The bank is parallel, stereo-linked in
  coefficients, with separate filter state for 3 and 4.
- **Sine Speech** — each track drives a sine oscillator at the track's frequency and
  amplitude. Summed, spread across the stereo field.
- **Both** — the two outputs summed.

## Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| Mode | vocoder / sine / both | vocoder | CC 5 |
| Formants | 1–8 | 4 | LPC order is 2N+2; CC 3 |
| Gate | −80…−20 dB | −55 | Below this the tracker reports silence |
| BW Limit | 100–3000 Hz | 700 | Resonances wider than this are not formants |
| Glide | 1–200 ms | 25 | Frequency and Q smoothing |
| Attack / Release | 1–100 / 5–500 ms | 6 / 70 | Amplitude follower |
| Shift | ±12 st | 0 | Transposes every formant; CC 1 |
| Q Scale | ×0.25–×4 | ×1 | Narrows or widens every band; CC 2 |
| Tilt | ±12 dB/oct | 0 | About 1 kHz; CC 4 |
| Level | ±24 dB | 0 | |
| Speech Mix | 0–100 % | 0 | Dry speech added to the output |
| Carrier Norm | 0–100 % | 100 | Divides band gain by carrier level so a hot carrier does not blow up |
| Sine Spread | 0–100 % | 30 | Low formants left, high right |
| Bypass | off / on | off | Speech passes straight through |

The panel's x-y pad puts Shift on X and Q Scale on Y.

## MIDI

**In** (on the *In Channel*): CC 1–5 as above — each controller number is a parameter
and 0 disables it. With **Note Shift** on, the last held note transposes the
formants by its distance from middle C (note 60), so a keyboard plays the vocal
tract's pitch. Releasing all notes returns to Shift alone.

**Out** (*MIDI Out*, on the *Out Channel*, sent after Shift and note shift, at most
every 10 ms):

- *formant CCs* — frequency of F1–F8 on CC 20–27 (80 Hz–8 kHz, logarithmic onto
  0–127) and level on CC 102–109 (−60…0 dB onto 0–127). Only changes are sent.
- *formant notes* — one note per formant at its nearest semitone, velocity from
  level, only while the formant is above −50 dB. A formant that wobbles over a
  semitone boundary re-triggers at most every 50 ms. Switching modes or channel
  releases every sounding note.

Use CCs to modulate other plugins from the voice's vowel; use notes to turn speech
into a chord stream (route to Magneto, Fathom or any synth).

## Good starting points

- Sine speech: Mode Sine Speech, Formants 3, Glide 20 ms. Needs nothing on 3/4.
- Robot choir: Mode Vocoder, a saw or pulse pad on 3/4, Formants 6, Q Scale ×0.5.
- Chipmunk / ogre: Shift +7 / −7 with the vocoder; the formants move, the carrier's
  pitch does not.
- Vowel to chord: MIDI Out → formant notes.

## Limits

- Tracks are LPC resonances, so they follow the *envelope*, not the pitch. A very
  high voice (f0 above ~300 Hz) can lock onto harmonics rather than formants.
- Fricatives are noisy and wide; raise BW Limit to let them through, and expect
  unsteady tracks.
- CC-driven changes reach the processor but the panel does not follow them.

See `docs/design.md` for the algorithm and the assumptions made.
