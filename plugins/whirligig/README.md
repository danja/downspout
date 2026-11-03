# Whirligig

A rotary speaker (Leslie-style). The input is summed to mono and split at a crossover into a low band for a **drum** rotor and a
high band for a **horn** rotor. Each rotor turns at its own speed with its own inertia, and two microphones hear it as a swell
(amplitude) and a wobble (Doppler pitch). Speeds run free in Hz, or lock to the host tempo and bar position (**Beat** mode).
Design and assumptions are in [docs/design.md](docs/design.md).

| Control | Range | Default | Notes |
|---|---|---|---|
| Speed | Stop, Slow, Fast | Slow | Rotors glide between speeds with their inertia. CC 1 and CC 2 take over until this is moved |
| Sync | Free, Beat | Free | Beat: speeds become beats per turn, phase-locked to the host BBT position |
| Horn / Drum Slow, Fast | 0.3-1.5, 4-9 / 0.3-1.2, 3-8 Hz | 0.8, 6.7 / 0.67, 5.7 | Free mode rates |
| Horn / Drum Slow, Fast Beats | 8, 4, 3, 2, 1.5, 1, 3/4, 1/2, 1/3, 1/4, 1/6, 1/8 beats per turn | 2, 1/3 / 3, 1/2 | Beat mode rates |
| Sync Phase | 0-360 deg | 0 | Beat mode: rotor angle at beat 0 |
| Horn / Drum Accel | 0.1-4 s / 0.5-10 s | 0.8 s / 3.5 s | Time constant of a speed change |
| Crossover | 300-2000 Hz | 800 Hz | Linkwitz-Riley 4th order |
| Horn / Drum Depth | 0-100 % | 60 % / 40 % | Swell |
| Horn / Drum Doppler | 0-100 % | 100 % / 80 % | Pitch wobble |
| Mic Angle | 0-180 deg | 90 | Between the left and right microphones |
| Horn / Drum Level, Output | -24..+12 dB | 0 | |
| Mix | 0-100 % | 100 % | Dry path is delay-matched |
| CC Channel | 0 (off), 1-16 | 1 | CC 1 (64+ Fast, else Slow), CC 2 (64+ Stop, else Slow) |

Audio: stereo in, stereo out (input summed to mono). MIDI in for CC 1 and 2; nothing is passed on. The transport is read for Beat mode;
with no valid transport it uses 120 BPM and free-runs. Latency: wet and dry both sit about 0.7 ms behind the input (not reported to the
host). Settings are ordinary automatable parameters; there is no plugin state. Five output parameters feed the panel.
