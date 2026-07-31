# Generative workstation practical test

This checklist covers Harmonic Atlas, Conductor, Drift, Mnemosyne, Polymeter,
Oracle, Mosaic, Resonance Garden, Orbit, and Guardian after the semantic UI
readiness pass.

## Build and install

Build the ten VST3 targets from a configured `build` directory:

```bash
cmake --build build --target \
  harmonic_atlas-vst3 conductor-vst3 drift-vst3 mnemosyne-vst3 \
  polymeter-vst3 oracle-vst3 mosaic-vst3 resonance_garden-vst3 \
  orbit-vst3 guardian-vst3
```

The bundles are under `build/bin/`. Copy the required bundles into the host's
VST3 directory and rescan plugins. Save a fresh test project before testing
state restoration.

## Shared UI checks

For every plugin:

- confirm named choices, switches, action buttons, musical note names, units,
  and read-only status are visually distinct;
- drag each continuous control, select every choice, and toggle every switch;
- right-click an editable control and confirm that it returns to its default;
- save, close, and reopen the project, then confirm parameter and plugin state;
- loop, seek, stop, restart, and change tempo while watching the live status.

## MIDI generators and processors

- **Harmonic Atlas:** route its MIDI output to an instrument. Compare all four
  movement languages, then enable input following and play new root notes.
  Confirm the keyboard/root display follows the generated harmony and that
  stop/seek produces no stuck notes.
- **Conductor:** route its MIDI/CC output to visible monitors or downstream
  generators. Compare Fixed and Weighted forms, shorten section durations, and
  confirm the section timeline changes only on bar boundaries.
- **Drift:** monitor all four CC destinations. Select each source type, confirm
  its preview and disabled controls make sense, and verify the event budget
  remains bounded at fast rates.
- **Mnemosyne:** in Listen mode, play at least one complete phrase. Confirm a
  reservoir slot fills, then compare Accompany and Autonomous modes and every
  transform. Clear memory, save/reload, and verify the empty and populated
  states behave correctly.
- **Polymeter:** route channel 10 to a drum instrument. Confirm each lane preview
  matches its length, pulses, rotation, and playhead. Try coprime lengths,
  ratchets, and transport restart and confirm the pattern repeats.

## Listener, instrument, and effects

- **Oracle:** provide audio and MIDI input and monitor its CC/note output.
  Confirm the analysis meters respond to silence, tone, percussion, and noise.
  Keep the response guard enabled when routing responses back into the patch.
- **Mosaic:** use Load WAV in each slot, then verify filename, loaded count, and
  missing-sample status. Test MIDI, Autonomous, and Both trigger modes; replace
  and clear samples; save/reload the project and confirm file paths restore
  without audio-thread stalls.
- **Resonance Garden:** feed short percussive audio, then hold and release MIDI
  notes. Verify the resonator display, internal fallback scale, freeze, voice
  limit, and wet/dry behavior. Check silence remains stable at high feedback.
- **Orbit:** use a sustained stereo source. Compare all four trajectory views,
  tempo changes, width, distance, and conservative Doppler. Confirm the marker
  moves smoothly and the effect is described as stereo rather than binaural.
- **Guardian:** place last on a deliberately hot signal chain. Confirm
  look-ahead latency, ceiling, gain-reduction and true-peak meters, soft
  clipping, silence indication, overload latch, and diagnostic reset. Feed a
  controlled non-finite test source only in a disposable test project.

## Ready criteria

The suite is ready for broader musical testing when all bundles scan, editors
open, controls remain legible at their default size, state restores, transport
operations do not leave notes or unsafe output, Mosaic paths restore, and
Guardian remains last in the output chain without host errors.
