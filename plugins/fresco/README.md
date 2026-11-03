# Fresco

A spectrogram painter. Load a picture and it is painted into the audio's spectrogram: the picture's height is frequency, its brightness is level, and a playhead
sweeps it left to right. Stereo in and out, **latency 2048 samples** (reported to the host), optional host-timeline playhead, no MIDI. Design and assumptions are in
[docs/design.md](docs/design.md).

| Section | Control | Range | Default | Notes |
|---|---|---|---|---|
| Picture | Load picture / Clear | PNG, PGM/PPM (P2 P3 P5 P6), BMP (8, 24, 32 bit) | built-in rings and cross | The path is the plugin's one state (`picture`); JPEG, GIF, WebP and video are refused with a message |
| Playback | Length | 0.5-120 s | 10 s | Time for the playhead to cross the picture |
| | Playhead | Free loop, Follow host | Free loop | Follow host: position = host time modulo Length (needs a valid tempo; otherwise free-running) |
| | Source | Input, Tone | Tone | Input keeps the input's phase; Tone is a bank of bin-centred sinusoids |
| | Follow input level | 0-100 % | 100 % | 100 %: silence in gives silence out. 0 %: the picture drones with no input |
| Mapping | Bottom, Top | 20 Hz-10 kHz, 200 Hz-20 kHz | 100 Hz, 16 kHz | Edges of the picture (Top is held at least 1.5 times Bottom) |
| | Axis | Linear, Log | Log | How rows map to frequency |
| | Range | 20-100 dB | 60 dB | Level of a nearly black pixel; black itself is silence |
| | Contrast | 0.25-4 | 1 | Gamma on the brightness |
| | Pixels | Bright = loud, Dark = loud | Bright = loud | |
| Output | Mix, Output | 0-100 %, -24..+12 dB | 100 %, 0 dB | The dry path is delayed by the latency |

Three output parameters (`status_playhead`, `status_level`, `status_loaded`) feed the panel. Settings are ordinary automatable parameters. Parameter ids 0-14
are fixed; later controls are appended.

To see the picture, put a spectrum analyser or spectrogram after Fresco, feed it white noise or a pad, and let the playhead run. A white pixel is a
0.02-amplitude sinusoid per frequency bin, so bright, crowded pictures are loud: trim with Output.
