# Campione Feature Requests

## Beat slicing on sample load

When loading a sample (e.g. a drum loop like `funky-drummer.wav`), offer optional
automatic beat slicing so each slice is spread across the keyboard as a separate zone.

The "Load File" prompt should present immediate treatment options:
- **Normalise** — peak-normalise the file before adding
- **Beat slice** — detect transients, split into slices, map each to successive keys
- **Crop ends** — trim silence/low-level content from head and tail

## Per-zone envelopes and filters

In the zone editor view, expose per-zone DSP controls:

### ADSR envelope
- Attack, Decay, Sustain, Release sliders
- Applied per voice, per zone

### Simple filter
- Type selector: LP / BP / HP / Notch
- Frequency and Q controls
- Applied per voice, per zone

Both envelope and filter parameters must be:
- Persisted in plugin state (serialized with zone data)
- Implemented in the DSP engine (lightweight — biquad filter, linear ADSR ramp)
