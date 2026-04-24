# cadence porting notes

Source plugin: `~/github/flues/lv2/cadence`

Notable behavior preserved in the current port:

- host-transport-synced learning over a repeating cycle;
- deterministic progression generation from captured note activity;
- voiced chord playback with register and spread controls;
- optional comp scheduling derived from captured onset timing;
- cycle-boundary variation driven by the `Vary` control;
- saved state for controls, progression, and variation progress.

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
- tighten UI wording and control feedback after DAW testing.
