# Chorale

A stereo chorus. The signal is delayed by a few milliseconds that sweep up and down, read in opposite directions
for the left and right outputs. By default each side has its own delay line, so a stereo source keeps its image. Three fixed Juno-style voicings (I, I+II, II) and a Custom chorus of one to four voices. Design and
assumptions are in [docs/design.md](docs/design.md).

| Control | Range | Default | Notes |
|---|---|---|---|
| Mode | I, I+II, II, Custom | I+II | The Juno voicings are fixed (sweeps of 0.513 and 0.863 Hz, 1.5-5.4 ms) |
| Tone | 0-100 % | 50 % | Opens both filters (pre 2-23.5 kHz, post 6-23.5 kHz, squared law) |
| Mix | 0-100 % | 50 % | Equal power. CC 4 |
| Rate | 0.05-8 Hz | 0.8 Hz | Custom. CC 1 (logarithmic) |
| Depth | 0-10 ms | 3 ms | Custom: sweep either side of Delay, held below Delay. CC 2 |
| Delay | 1-30 ms | 8 ms | Custom: centre delay |
| Voices | 1-4 | 2 | Custom: spread round the cycle |
| Width | 0-100 % | 100 % | 0 is a mono wet signal. CC 3 |
| Input | Mono sum, Stereo | Stereo | Stereo: a delay line per side. Mono sum: L + R into one line, as in the Juno source |
| CC Channel | 0 (off), 1-16 | 1 | CC 1-4 take over Rate, Depth, Width, Mix until the control is moved again |

Audio: stereo in, stereo out. MIDI in for CC 1-4; nothing is passed on. No latency: the dry path is not delayed. The wet signal is twice its input (Mono sum: L + R), so it sits about 6 dB above the dry for a centred signal, the same in both Input settings. Settings are ordinary automatable parameters; there is no
plugin state. Eight output parameters feed the panel.
