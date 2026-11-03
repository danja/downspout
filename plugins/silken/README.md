# Silken

A vocal clean-up and polish strip: rumble filter, plosive guard, de-esser, warmth / presence / air EQ, sparkle, output level. Stereo in and out,
no latency, no MIDI. Every stage is neutral when it is off or at zero, and the strip is then the input, sample for sample. Design and assumptions
are in [docs/design.md](docs/design.md).

| Stage | Control | Range | Default | Notes |
|---|---|---|---|---|
| Rumble filter | Rumble Filter, Rumble Freq | Off/On, 20-300 Hz | On, 80 Hz | 2nd-order Butterworth high-pass |
| Plosive guard | Plosive Guard, Plosive Threshold | Off/On, -40..0 dB | On, -20 dB | Watches the band below 150 Hz (6:1); when it fires, fades towards a 150 Hz high-pass |
| De-esser | De-esser | Off/On | On | |
| | Ess Frequency | 2-12 kHz | 6.5 kHz | |
| | Ess Threshold | -60..0 dB | -30 dB | Detector level above which it acts (4:1, soft knee) |
| | Ess Range | 0-24 dB | 10 dB | The most it will take off |
| | Ess Mode | Band, Wide | Band | Band: a dynamic bell cut at the frequency. Wide: the whole signal |
| | Ess Listen | Off/On | Off | Plays the detector |
| Voice EQ | Warmth, Presence, Air | -12..+12 dB | 0 | Low shelf 200 Hz, bell 3.2 kHz (Q 0.9), high shelf 10 kHz |
| Sparkle | Sparkle | 0-100 % | 0 | Harmonics of the band above 5 kHz, added above 8 kHz |
| | Output | -24..+12 dB | 0 | |

Three output parameters (`status_ess`, `status_plosive`, `status_detector`) feed the panel's meters. Settings are ordinary automatable parameters;
there is no plugin state.
