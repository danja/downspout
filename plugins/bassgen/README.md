# bassgen port

This directory will hold the `downspout` port of `~/github/flues/lv2/bassgen`.

Near-term work:

- audit the LV2 implementation;
- extract transport, pattern, variation, and state logic into portable core code;
- map parameters and state into a DPF/VST3-friendly shape;
- add host integration after the core behavior is covered by tests.

Current reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`

Implementation status:

- portable core library exists and builds;
- deterministic tests exist and pass;
- host-neutral block engine exists for MIDI scheduling;
- a first DPF wrapper with UI builds as `bassgen.vst3`;
- wrapper validation in a real host is still pending.
