# Scope

Spectrum analyser and level meter. Audio passes through untouched.

## Display
- **Spectrum**: 64 log-spaced bands from 20 Hz to 20 kHz with labelled frequency grid
  (50 Hz to 20 kHz) and a 60/90/120 dB scale.
- **Levels**: peak and RMS meters per channel plus a correlation meter.
- Peak-hold markers on both displays.

## Controls
- **Range**: 60, 90 or 120 dB display range.
- **Speed**: analyser and meter ballistics.
- **Peak hold**: off/on.
- **Display**: spectrum or levels.

Magneto look. No latency, no plugin state.

The frequency grid is a fixed 20 Hz-20 kHz log axis, so at sample rates above 40 kHz the labels
match the bands. Below 40 kHz the analyser's top band is the Nyquist frequency while the axis
still reads 20 kHz.
