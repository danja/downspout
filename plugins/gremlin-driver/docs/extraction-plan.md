# gremlin-driver extraction plan

## Completed

1. Copy the portable modulation engine into plugin-local include space.
2. Define framework-neutral parameter tables and names.
3. Re-host transport handling, pass-through, CC emission, trigger-note emission,
   and patch-randomise bursts in a portable processor.
4. Add a DPF VST3 wrapper and custom UI.
5. Add a focused processor smoke test.

## Remaining follow-up

1. Validate DAW routing expectations for MIDI-only VST3 utility plugins.
2. Verify `gremlin_driver -> gremlin` chaining in Reaper with real transport.
3. Decide whether later builds should expose preset scenes for modulation
   layouts.
