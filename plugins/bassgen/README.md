# bassgen port

This directory holds the `downspout` port of `~/github/flues/lv2/bassgen`.

`bassgen` is a transport-aware MIDI bass generator with persistent pattern
state, loop-boundary variation, and a custom DPF UI. In `downspout`, the core
now stores normalized meter data and shapes rhythm around pulse starts,
pickups, and longer pulse-spanning notes rather than assuming every bar is
effectively `4/4`. It also now exposes explicit style modes:
`Auto`, `Straight`, `Reel`, `Waltz`, `Jig`, and `Slip Jig`.

Implementation status:

- portable core library exists and builds;
- deterministic tests exist and pass;
- host-neutral block engine exists for MIDI scheduling;
- text state serialization exists for controls, pattern, and variation;
- the DPF wrapper and custom UI build as `bassgen.vst3`;
- the core now handles shared-meter transport input and regenerates on meter
  changes;
- the core now has explicit style controls for simple, reel, waltz, jig, and
  slip-jig phrasing on top of the shared meter model.

Reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`
- `docs/porting.md`
