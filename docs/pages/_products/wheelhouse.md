---
title: Wheelhouse
order: 237
bundle: wheelhouse.vst3
kind: Instrument
category: instrument
group: synths
role: Tonewheel organ
screenshot: /assets/plugins/wheelhouse.png
capabilities: [MIDI input, audio output]
summary: A tonewheel organ with 91 tone wheels, nine drawbars for each of two manuals and two for the pedals, percussion, scanner vibrato and chorus, key click and overdrive.
---

## Opinion

Not yet auditioned in a host. The defaults are the classic jazz registration, 888000000, on the upper manual with C3 chorus and a little drive, and a soft
008530000 on the lower. Switch Percussion on (third harmonic, fast) for the bright, short attack on a phrase, pull the 1' drawbar and notice it goes quiet while
percussion is on, as it should. Put a rotary speaker after it for the full sound: there is no Leslie in here.

## Functionality

Ninety-one tone wheels turn all the time. A held key connects nine footages (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1') to the output through the **drawbars**,
set 0 to 8 in 3 dB steps. The **upper** and **lower** manuals have nine drawbars each and the **pedals** two; each answers its own MIDI channel (1, 2 and 3 by default;
0 turns one off). Notes above the top wheel fold back by octaves, so the top of the keyboard stays in tune. **Percussion** (upper manual) adds a decaying second or third
harmonic on the first key of a legato phrase. **Vibrato/Chorus** is a scanner: V1-V3 vibrato and C1-C3 chorus at 4-9 Hz on the upper and/or lower manual. **Key Click**,
**Drive**, **Tone**, **Width** and **Volume** finish the sound, and CC 11 is the swell pedal.

Keys: upper and lower C2-C7, pedals C2-G4. Velocity is ignored, as on an organ.

### Parameters

| Parameter | Range | Default |
|---|---|---|
| Upper drawbars | 0-8 x 9 | 888000000 |
| Lower drawbars | 0-8 x 9 | 008530000 |
| Pedal drawbars | 0-8 x 2 | 8 0 |
| Percussion | Off, On | Off |
| Perc Harmonic / Decay / Level | 2nd, 3rd / Fast, Slow / Soft, Normal | 3rd / Fast / Normal |
| Vibrato | Off, V1-V3, C1-C3 | C3 |
| Vibrato Upper / Lower | Off, On | On / Off |
| Scanner Rate | 4-9 Hz | 6.9 Hz |
| Key Click | 0-100 % | 30 % |
| Drive | 0-100 % | 15 % |
| Tone | 0-100 % | 100 % |
| Width | 0-100 % | 50 % |
| Volume | 0-100 % | 70 % |
| Upper / Lower / Pedal Channel | off, 1-16 | 1 / 2 / 3 |

### The panel

Three banks of drawbars, pulled down to add a footage (click or drag a bar, or scroll over it), with the console's brown, white and black caps. The Jazz, Full, Flute
and Reed buttons set a manual's drawbars in one click. Percussion, the scanner, the sound controls and the three MIDI channels sit underneath, and controls that do
nothing in the current mode are dimmed.

### Status

Core covered by tests (104 checks): the wheel mapping and foldback against a hand-written table, each footage's pitch, every drawbar step, the divisions and channels,
percussion, the scanner sidebands against the FM formula, key click, drive, tone, levels, block-size independence and bad input. The voicing (percussion times, scanner
depths, click level) is by ear. Not yet listened to or validated in a host.
