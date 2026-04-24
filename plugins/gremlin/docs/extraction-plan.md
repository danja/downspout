# gremlin extraction plan

## Completed

1. Copy the portable engine into plugin-local include space.
2. Define framework-neutral parameter/mapping tables.
3. Port scenes, macros, momentaries, actions, and controller mapping into a
   host-neutral processor.
4. Add a DPF VST3 wrapper and a custom UI.
5. Add a focused processor smoke test.

## Remaining follow-up

1. Host-validate MIDI controller gestures in Reaper.
2. Decide whether saved-state text serialization is worth adding beyond host
   parameter persistence.
3. Decide whether a second controller-output path is worth adding in a later
   standalone or CLAP build.
