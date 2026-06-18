# Basilico implementation notes

Basilico is an original Downspout instrument rather than a port from `flues`.

## Model contract

The public `Model` parameter selects a profile over one engine:

- `Upright`: triangle-like tone, body emphasis, low drive.
- `Electric`: balanced oscillator/sub/body path.
- `Dub`: muted low-frequency focus with rolled highs.
- `Acid`: resonant filter envelope and stronger drive.
- `Industrial`: hard drive/fold behavior with more bite.

The model can force a waveform or drive type internally, but the host-facing
parameter table remains stable.

## MIDI behavior

Basilico is monophonic with last-note priority. Overlapping notes glide when
the `Glide` parameter is active. Velocity affects output level, punch, and
filter accent.

## Wobble and squelch

Wobble support is implemented as plugin-local portable DSP rather than DPF
glue. `basilico_modulation` generates the free-running or tempo-synced wobble
shape, and the engine fans that signal out to amplitude, filter cutoff pitch,
and `basilico_flanger` stereo phase/flange modulation.

The original `lfo_frequency` and `lfo_depth` parameter slots are preserved as
the free wobble rate and filter wobble target. New controls are appended after
the original table: sync mode, division, shape, amp wobble, phase wobble, and
squelch. `Squelch` is an Acid-compatible macro that increases resonant filter
movement, shortens filter snap, and adds drive without changing the selected
model.

## Tests

`downspout_basilico_core_tests` covers:

- defaults and clamping;
- all models rendering;
- note-off release;
- MIDI pitch tracking;
- glide behavior;
- velocity accent behavior;
- output boost and output bounding;
- filter, amplitude, and phase/flange wobble;
- tempo-synced wobble rate;
- acid squelch tone changes.
