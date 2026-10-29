# Infra

A stereo VCA driven by a beat-locked LFO: a tremolo whose speed is a whole multiple of the host's beat rate, kept between
1 and 20 Hz. Eight waveforms, a separate 0 / 90 / 180 / 270 degree phase for each channel, and a VCA range that defaults to
0 dB down to -3 dB. Design and assumptions are in [docs/design.md](docs/design.md).

| Control | Range | Default | Notes |
|---|---|---|---|
| Waveform | Sine, Triangle, Ramp up, Ramp down, Square, Soft square, Sample & hold, Smooth random | Sine | All rise first and peak a quarter cycle in; the random shapes pick a fixed value per cycle |
| Multiple | 0 (Auto), 1-40 cycles per beat | Auto | Auto is the whole multiple whose rate is nearest to 10 Hz (5 at 120 BPM). Any request is moved into the range that keeps the rate within 1-20 Hz |
| Phase Left / Right | 0, 90, 180, 270 degrees | 90 / 90 | Offsets that channel's LFO; at 90 degrees the LFO is at its peak (0 dB) on every beat |
| Range | 0-40 dB | 3 dB | The VCA swings from 0 dB (LFO peak) down to -Range (LFO trough); 0 is transparent |

Audio: stereo in, stereo out, no latency, no MIDI. The LFO is a function of the transport position, so it repeats exactly on
every pass and does not depend on the block size. While the transport is stopped, or when the host reports no tempo, it
free-runs (at 120 BPM when there is no tempo). Settings are ordinary automatable parameters; there is no plugin state. Four
output parameters (`status_rate`, `status_multiple`, `status_lfo_l`, `status_lfo_r`) feed the panel.
