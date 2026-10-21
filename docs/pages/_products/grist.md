---
title: Grist
order: 216
bundle: grist.vst3
kind: Audio effect
category: processor
role: Granular grain delay
screenshot: /assets/plugins/grist.png
capabilities: [audio input, audio output, tempo sync]
summary: A grain delay. The input is cut into windowed grains, each with its own start, direction, length, position and pitch (optionally held to a scale), and the result can feed back through a filter.
---

## Opinion

Not yet auditioned in a host. Expect it to be most at home on sustained or rhythmic material:
with **Density** 2, **Blur** at 0 and **Pitch** at 0 it is transparent, and every control you
add from there (Blur for smear, Range on a Scale for shimmering intervals, Reverse for
swells, Feedback for trails) moves it further from the source.

It delays the sound by design, so it belongs on a send or a parallel track rather than across
a part that has to stay in time. The panel shows the average latency.

## Functionality

The input is recorded into a seven-second history. One grain starts every **Window** divided
by **Density**, and each plays a slice of that history under a window shape, so grains keep
scrolling even at Pitch 0. **Window** runs from 1 ms to 1 s, or from a tempo division (1/64 to
8/1, with triplets and dotted values) when set to sync.

Each grain draws its own values from a seeded generator, so a pass repeats exactly for a given
**Seed**:

- **Blur** starts it a random distance back, up to the set number of windows.
- **Reverse** is the chance it plays backwards.
- **Spread** places it in the stereo field.
- **Variance** shortens it, down to a quarter of the window.
- **Range** moves its pitch up, down or both (**Direction**) by up to two octaves around
  **Pitch**. **Scale** limits where it can land: Free, Semitones, Octaves, Fifths and
  octaves, Major, Minor or Pentatonic, counted from the pitch of the incoming sound.

**Shape** blends a Hann bell with a percussive curve; both have the same average level.
Pitching up needs a head start (a grain that read faster than real time would run into the
present), so the average latency is Blur x Window / 2, plus (ratio - 1) x Window when pitched
up. It is shown on the panel and is not reported to the host.

**Feedback** returns the grain output through its own **Delay** (free or tempo-synced) and an
optional lowpass, highpass or bandpass filter, back into the history. A soft limiter keeps the
loop bounded, but it can build, so trim the output.

### With a resonator

Grist does no resonating of its own. For tuned, ringing results, put
[Resonance Garden](../resonance-garden/) after it: grains in, damped resonators tuned by held
notes out. [Moka](../moka/) does not fit this role, because it is a MIDI-triggered instrument
with no audio input.

[Beards](../beards/) is the other granular effect here. It adds freeze, reverb, a quality
setting and Drift/Conductor control; Grist adds tempo-locked windows, a scale-aware pitch
cloud and a filtered feedback loop.
