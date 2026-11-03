# Wheelhouse

A tonewheel organ. Ninety-one tone wheels turn all the time and a held key connects nine footages to the output through
drawbars. Two manuals and a pedalboard, each on its own MIDI channel, plus percussion, scanner vibrato and chorus, key
click, overdrive and a swell pedal. Design and assumptions are in [docs/design.md](docs/design.md).

| Control | Range | Default | Notes |
|---|---|---|---|
| Upper drawbars (9) | 0-8 each | 8 8 8 0 0 0 0 0 0 | 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'. 0 is off, then 3 dB per step to 8 |
| Lower drawbars (9) | 0-8 each | 0 0 8 5 3 0 0 0 0 | Same footages |
| Pedal drawbars (2) | 0-8 each | 8 0 | 16' and 8' |
| Percussion | Off, On | Off | Upper manual only; silences the 1' drawbar |
| Perc Harmonic / Decay / Level | 2nd or 3rd; Fast (0.35 s) or Slow (1.5 s); Soft or Normal | 3rd / Fast / Normal | Fires on the first key of a legato run |
| Vibrato | Off, V1-V3, C1-C3 | C3 | V is vibrato, C is chorus; the number is the depth (0.20, 0.35, 0.55 ms) |
| Vibrato Upper / Lower | Off, On | On / Off | Which manual goes through the scanner; the pedals never do |
| Scanner Rate | 4-9 Hz | 6.9 Hz | |
| Key Click | 0-100 % | 30 % | Noise burst on every key press and release |
| Drive | 0-100 % | 15 % | Preamp overdrive; 0 is a bypass |
| Tone | 0-100 % | 100 % | Low-pass from 1.5 kHz up to 16 kHz; 100 % is open |
| Width | 0-100 % | 50 % | Offsets the right channel's scanner phase |
| Volume | 0-100 % | 70 % | |
| Upper / Lower / Pedal Channel | 0 (off), 1-16 | 1 / 2 / 3 | |

Keys: upper and lower C2-C7 (MIDI 36-96), pedals C2-G4 (36-67); notes outside are ignored. Velocity is ignored. CC 11 is the
swell pedal (squared); CC 123 and CC 120 release the keys on their channel.

Audio: no input, stereo out. MIDI in. The organ itself is mono: the two channels differ only through the scanner's Width.
Settings are ordinary automatable parameters; there is no plugin state. There are no output parameters.
