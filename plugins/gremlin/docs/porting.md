# gremlin porting notes

## UI direction

The LV2 X11 UI mirrored the MIDImix layout closely. The VST3 UI keeps the same
performance ideas but uses a DAW-friendly panel with tall, narrow fader blocks:

- mode buttons
- scene buttons
- action buttons
- macro faders
- source faders
- time/space faders
- momentary hold pads

This is intentionally a functional reinterpretation, not a literal X11 port.

## Sound mode direction

The mode DSP intentionally diverges after the initial port so the six sources do
not collapse into one generic glitch sound:

- `Shard`: short, bright, edge-weighted transients with restrained delay.
- `Servo`: smoother pitched malfunction with reduced crush and fold.
- `Spray`: particulate noise bursts, stronger decimation, and short glitches.
- `Collapse`: darker low/body instability with heavier folding and feedback.
- `Ring`: metallic cross-modulation and comb-like delay behavior.
- `Vapor`: softer smeared cloud tones with longer, wetter delay.

Factory scenes and source randomisation are biased toward those identities
rather than using one broad midrange set of breakage values for every mode.
Normal-to-moderate settings keep the additional voices locked to mode-specific
interval sets so pitched notes read more musically. High combined damage, fold,
feedback, stutter, and crunch introduce a smoothed catastrophe layer with
stronger foldback, alarm tones, denser glitch holds, and wetter cross-feedback
so pushed patches become much more extreme.

## MIDI behavior

- note input still plays the synth
- mapped controller CCs update live, hidden, macro, and master controls
- mapped controller notes trigger momentaries, scenes, actions, and mode steps
- the old LV2 controller-output LED path is represented as VST3 MIDI output
  for MIDImix button LEDs

## Current known gap

There is no custom state serialization yet. Hosts will persist parameters, but
there is no extra compatibility layer for future parameter-layout changes.

Gremlin also reports live controller state through output/status parameters so
the UI can mirror controller gestures. For Reaper MIDI output routing, see
[reaper-midimix.md](reaper-midimix.md).
