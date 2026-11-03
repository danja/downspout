# Silken

A vocal strip in signal order: rumble filter, plosive guard, gate, harshness tamer, de-esser, leveller, voice EQ, drive, sparkle, output. Stereo in and out,
no latency, no MIDI. Every stage is neutral when it is off or at zero, and the strip is then the input, sample for sample. Design and assumptions
are in [docs/design.md](docs/design.md).

| Stage | Control | Range | Default | Notes |
|---|---|---|---|---|
| Rumble filter | Rumble Filter, Rumble Freq | Off/On, 20-300 Hz | On, 80 Hz | 2nd-order Butterworth high-pass |
| Plosive guard | Plosive Guard, Plosive Threshold | Off/On, -40..0 dB | On, -20 dB | Watches the band below 150 Hz (6:1); when it fires, fades towards a 150 Hz high-pass |
| Gate | Gate, Gate Threshold, Gate Range | Off/On, -80..-20 dB, 0-40 dB | Off, -50 dB, 12 dB | 2:1 downward expander on the louder channel; closes over 40 ms, opens in 2 ms |
| Harshness | Harsh Amount, Harsh Frequency, Harsh Threshold | 0-12 dB, 1.5-5 kHz, -60..0 dB | 0 (off), 3 kHz, -30 dB | The de-esser's mechanism at a lower frequency (3:1): a dynamic bell cut |
| De-esser | De-esser | Off/On | On | |
| | Ess Frequency | 2-12 kHz | 6.5 kHz | |
| | Ess Threshold | -60..0 dB | -30 dB | Detector level above which it acts (4:1, soft knee) |
| | Ess Range | 0-24 dB | 10 dB | The most it will take off |
| | Ess Mode | Band, Wide | Band | Band: a dynamic bell cut at the frequency. Wide: the whole signal |
| | Ess Listen | Off/On | Off | Plays the detector |
| Leveller | Leveller, Leveller Threshold, Leveller Ratio, Leveller Mix | Off/On, -40..0 dB, 1.5-10, 0-100 % | Off, -20 dB, 3, 100 % | Compressor, 8 ms attack, 150 ms release, soft knee, half automatic make-up, parallel mix |
| Voice EQ | Mud | -12..+12 dB | 0 | Bell, 300 Hz, Q 1 |
| | Warmth, Presence, Air | -12..+12 dB | 0 | Low shelf 200 Hz, bell 3.2 kHz (Q 0.9), high shelf 10 kHz |
| Drive | Drive | 0-100 % | 0 | Soft saturation, unity for small signals |
| Sparkle | Sparkle | 0-100 % | 0 | Harmonics of the band above 5 kHz, added above 8 kHz |
| | Output | -24..+12 dB | 0 | |

Five output parameters (`status_ess`, `status_plosive`, `status_detector`, `status_gate`, `status_level`) feed the panel's meters. Settings are ordinary
automatable parameters; there is no plugin state. Parameter ids 0-17 are from the first version and never change; later controls are appended.
