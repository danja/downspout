# bassgen port

This directory holds the `downspout` port of `~/github/flues/lv2/bassgen`.

`bassgen` is a transport-aware MIDI bass generator with persistent pattern
state, loop-boundary variation, and a custom DPF UI. In `downspout`, the core
now stores normalized meter data and shapes rhythm around pulse starts rather
than assuming every bar is effectively `4/4`.

Implementation status:

- portable core library exists and builds;
- deterministic tests exist and pass;
- host-neutral block engine exists for MIDI scheduling;
- text state serialization exists for controls, pattern, and variation;
- the DPF wrapper and custom UI build as `bassgen.vst3`;
- the core now handles shared-meter transport input and regenerates on meter
  changes;
- musical style behavior in compound meter is improved but still not
  folk-specific.

Reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`
- `docs/porting.md`
