# drumkit porting notes

Source: `/home/danny/github/flues/lv2/drumkit`.

The LV2 plugin was a direct wrapper around a header-only drum engine. The Downspout port keeps that engine host-neutral under `plugins/drumkit/include` and moves host details into `src/dpf`.

Mapping decisions:

- LV2 Atom MIDI input maps to DPF MIDI input.
- The two LV2 audio outputs map to one stereo DPF output group.
- LV2 control ports 2-44 map to DPF automatable parameters 0-42 in the same semantic order.
- Added mute parameters are appended after the inherited controls to keep the original parameter block traceable.
- The kick transient amount is appended after the mute parameters. It is a
  Downspout-only extension, defaulting to zero so existing drumkit patches keep
  the inherited kick tone unless the new slider is raised.
- Metal controls for Clap, Snare, Crash, Closed Hat, Low Tom, Open Hat, High
  Tom, Cowbell, and Clave are appended after Kick Transient. They default to
  zero, preserving the inherited voice tone unless raised.
- The LV2 X11 UI was not ported. The DPF UI is new and intentionally separates mixer actions from voice editing.

UI decisions:

- The LV2 X11 UI was discarded because it did not match the control density or
  workflow expected for the VST3 port.
- The replacement UI uses mixer-style instrument strips for fast selection,
  level balancing, MIDI-note reference, and mute toggles.
- Only the selected voice exposes its detailed synthesis controls at once,
  which keeps the interface usable despite the large inherited parameter set.
- Master crush, drive, reverb, and gain live in a separate panel because they
  affect the shared bus and remain active even when individual voices are
  muted.
- The kick pitch slider remains stored as the inherited normalized value, but
  the custom UI formats it through the same 30-250 Hz mapping used by the
  portable kick voice. The mapping is anchored so the inherited default remains
  near its previous pitch while the minimum reaches 30 Hz.

Resource behavior:

- Each mute parameter is owned by the portable engine, not the UI.
- A muted instrument skips trigger handling and per-sample voice processing.
- Enabling a mute resets the selected voice so it does not continue advancing an envelope while inaudible.
- Master bus processing remains shared and always runs, preserving the LV2 bus character and reverb behavior.
