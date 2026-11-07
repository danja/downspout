# Scope design notes

- STFT: 2048-point FFT, sqrt-Hann window, hop 512 (75 % overlap).
- Bins are collapsed into 64 log-spaced bands from 20 Hz to 20 kHz. Each band takes
  the loudest bin inside it, so narrow peaks remain visible at every frequency.
- One-pole smoothing per bin (`Speed`); the first complete window primes the display
  instead of fading in from the floor.
- Normalisation divides by the window's coherent gain, so a full-scale sine reads 0 dB.
- Peak (slow release), RMS (one-pole) and correlation are measured from the stereo
  input. The first four parameters are interactive; the rest are read-only analysis
  outputs pushed to the UI, so the VST3 can show live data.
- Display range is 60, 90 or 120 dB. Frequency labels are placed on the log axis at
  50, 100, 200, 500, 1k, 2k, 5k, 10k and 20k.
- Audio is copied from input to output, so the analyser is safe anywhere in a chain.
- No plugin state. Listen/audit in a host still pending.
