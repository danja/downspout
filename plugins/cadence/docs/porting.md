# cadence porting notes

Source plugin: `~/github/flues/lv2/cadence`

Notable behavior preserved in the current port:

- host-transport-synced learning over a repeating cycle;
- deterministic progression generation from captured note activity;
- voiced chord playback with register and continuous spread controls;
- arpeggiated playback phrasing that can thin chord hits down to rotating
  single-note chord tones;
- optional comp scheduling derived from captured onset timing;
- cycle-boundary variation driven by the `Vary` control;
- harmonic color bias driven by the `Color` control, defaulting to zero so
  earlier behavior remains the baseline until raised;
- extended chord slots for the new `Extended` chord-size mode, allowing
  generated 9th, 11th, and 13th voicings;
- saved state for controls, progression, and variation progress.

Learning and reset behavior:

- Cadence learns continuously while transport is playing. At each cycle
  boundary, the current capture updates the base progression and the capture is
  cleared for the next cycle.
- The explicit `Learn` action and timing-grid changes (`Cycle`, `Segment`) reset
  learned material because they change the capture contract.
- Harmony and voicing changes rebuild from the last learned capture when
  available, or revoice a restored saved progression when raw capture is not
  available. This keeps the plugin ready instead of dropping into an empty
  Learning state after ordinary sound-shaping changes.
- The DPF wrapper exposes `MIDI Input Activity` and `MIDI Output Activity`
  output parameters, mirrored in the UI header, to diagnose host MIDI routing.

Jazz note:

- Cadence now stores up to six notes per chord slot. Old four-note progression
  state remains readable because missing extension notes default to zero and
  `note_count` is serialized explicitly. The new extension qualities are only
  considered when `Chord` is set to `Extended`.
- `Spread` and `Arpeggio` are playback-facing controls. Spread changes the
  generated voicing width, while Arpeggio leaves stored chord slots intact and
  changes which chord tones are emitted on each comp hit.

Current wrapper choice:

- the first DPF/VST3 wrapper has a custom control UI rather than relying on a
  generic host parameter view;
- the UI keeps the original control surface compact and direct instead of trying
  to recreate the old X11 interface exactly;
- no extra visualization or redesign work has been added ahead of host
  validation.

Known follow-up areas:

- verify learn and ready behavior in Reaper against the original LV2 plugin;
- confirm restart and rewind handling in looped playback;
- validate high `Arpeggio` settings with `Comp` and dense Extended voicings.
