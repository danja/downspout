# Fathom

`fathom.vst3` — drone synthesizer.

A four-voice instrument built for sustained, slowly moving sound. Notes play a
generator of clustered oscillators, a per-voice notch filter shapes it, and a
delay network with a frequency shifter in its feedback loop turns it into a
spiralling wash. Stereo out, MIDI notes and controllers in.

It is modelled on the signal flow described in the Vhikk X algorithm reference
from FORGE TME (<https://forge-tme.com/vhikk-x/algos_launch.html>): generator,
filter, VCA, then a delay-based processor with an integrated frequency shifter.
It is an original implementation of that idea, not a port, and borrows the
control names where they fit. What was kept, changed and left out is in
[docs/design.md](docs/design.md).

## Using it

Fathom is an instrument: no audio input, stereo output. It is silent until a note
arrives. Attack and release are long by default, so notes fade in and out.

**Generator.** Pick an algorithm, then shape it with five controls. The same five
sliders do something different in each algorithm, and the panel's slider names
change to say what:

| Algorithm | Warp | Span | Morph | Seed | Fuse |
|-----------|------|------|-------|------|------|
| Analog | FM of the saw by the pulse | pulse detune | saw/pulse to sine | unused | pulse width |
| Shaper | phase modulation | modulator ratio | to ring mod | unused | wavefold |
| Sines | bit crush | partial drift | spectral tilt | drift pattern | saturation |
| Table | neighbour phase mod | tuning fan | wavetable scan | frame offsets | wavefold |
| Cluster | ring modulation | interval width | scan window | which intervals | sine to saw |

At Span 0 every algorithm sits on the played pitch. Seed scans smoothly through a
fixed set of random offsets, so it is repeatable and a small move is a small change.

**Filter.** **Field** sets a low-pass from 40 Hz to 20 kHz; **Notch** carves two notches
below it. Per voice, before the envelope.

**Processor.** **Metallic** is a four-line feedback delay network; **Diffuse** is a
stereo ping-pong delay smeared by allpass diffusers. **Time** is the base delay length
(10 ms to 0.8 s), **Form** moves from distinct echoes to a diffuse wash, **Feed** sets
how long it rings, **Shift** moves every repeat up or down in pitch by up to 500 Hz, and
**Mix** blends dry and wet. Shift at the centre is off.

**Voices and output.** **Basis** transposes by up to two octaves, **Glide** slides between
notes, **Attack** (5 ms to 5 s) and **Release** (10 ms to 10 s) shape the fade, **Hold**
keeps notes sounding after the keys come up, **Level** sets the output, and **MIDI in**
picks the channel (Off, 1-16 or All).

## MIDI

| Message | Action |
|---------|--------|
| Note on/off | Play a voice (four, with voice stealing). Velocity shades the level from 60% to 100% |
| Pitch bend | +-2 semitones |
| CC 1 | Warp |
| CC 2 | Span |
| CC 3 | Morph |
| CC 4 | Seed |
| CC 5 | Fuse |
| CC 6 | Field |
| CC 7 | Level |
| CC 8 | Notch |
| CC 9 | Mix |
| CC 10 | Time |
| CC 11 | Form |
| CC 12 | Feed |
| CC 13 | Shift (bipolar) |
| CC 14 | Basis |
| CC 64 | Hold (sustain pedal) |
| CC 120 / 123 | All sound off / all notes off |

CC 1-4 are the numbers [Drift](../drift/) emits from its four lanes by default, so Drift
animates Warp, Span, Morph and Seed with no setup. A controller moves the sound but not the
on-screen slider; DPF has no DSP-side path back to the panel.

## Build and test

```bash
cmake --build build --target downspout_fathom_core_tests fathom-vst3
ctest --test-dir build -R fathom --output-on-failure
```

The core (`include/`, `src/*.cpp`) has no DPF dependency; the VST3 wrapper and the panel
are in `src/dpf/`. Host validation in REAPER has not been done.
