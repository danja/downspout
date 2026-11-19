# Tilt

Linked stereo master-bus tone control. One **Pivot Freq** and one **Angle** set a
straight line of gain in dB against log frequency; **Slope** chooses whether a
full Angle is 6, 12 or 24 dB per octave. The line is clamped to +-24 dB at the
ends. No MIDI, transport or plugin state.

The panel draws the target line faintly and the actual response brightly over a
64-band live spectrum. Drag across the plot for Pivot Freq, up and down for
Angle, or scroll over it for fine angle. **Mode** switches between:

- **IIR** — minimum-phase, zero latency. Double-precision shelf/ladder cascade,
  normalised at the pivot.
- **FFT** — linear phase, 4096 samples of reported latency. A 4096-point
  sqrt-Hann STFT with hop 512 applies the exact straight-line gain per bin.

Both channels always receive the same tilt. Control changes and mode/bypass
switches glide over about 50 ms; the IIR and dry paths are delayed to align with
the STFT during a switch.

| Control | Range | Default |
|---|---|---|
| Pivot Freq | 20 Hz to 20 kHz (logarithmic) | 1 kHz |
| Angle | -1 to +1 | 0 (transparent) |
| Slope | 6 / 12 / 24 dB per octave | 6 dB |
| Mode | IIR / FFT | IIR |
| Output | -12 to +12 dB | 0 dB |
| Bypass | Off / On | Off |
| Spectrum | Off / Pre / Post | Post |

The seven controls are ordinary automatable host parameters. Outputs are
read-only: Latency (ms), Peak L, Peak R and 64 `spectrum_NN` bars. The analyser
is a separate 4096-point Hann FFT of the mono sum, refreshed about 30 times a
second and folded into log-spaced bands; a full-scale sine reads 0 dB at its
peak.

Core tests (`downspout_tilt_core_tests`) cover the parameter law, exact default
pass-through, measured IIR and FFT response, slope and pivot audibility, linked
stereo, FFT unity round trip and latency, bypass, output gain and block-size
invariance. See `docs/design.md` for the design decisions.
