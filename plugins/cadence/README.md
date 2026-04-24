# cadence port

This directory holds the `downspout` port of `~/github/flues/lv2/cadence`.

Current focus:

- preserve the learned-progression and transport-synced harmony behavior from the
  LV2 source;
- keep the harmony and variation logic portable and testable outside DPF;
- keep the DPF wrapper thin around transport, MIDI, state, and UI plumbing;
- validate the first VST3 wrapper in a real DAW before widening scope.

Current reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`
- `docs/porting.md`

Implementation status:

- plugin scaffold now exists in `downspout`;
- portable core types, harmony generation, comping, transport, variation, and
  serialization code now exist;
- a host-neutral cadence engine now exists;
- deterministic cadence core tests now exist and pass;
- a first DPF-backed `cadence.vst3` wrapper now builds with a custom control UI.

Recommended next steps:

1. validate `cadence.vst3` in Reaper, especially the learn cycle, restart/rewind
   handling, and saved-state restore path;
2. tighten any UI or parameter behavior issues found in host use;
3. broaden tests around legacy state migration and wrapper-facing state mapping;
4. keep release packaging aligned with the now-public wrapper target.
