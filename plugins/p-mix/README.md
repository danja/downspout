# p-mix port

This directory will hold the `downspout` port of `~/github/flues/lv2/p-mix`.

Near-term work:

- isolate transport/time-position handling;
- extract state and transition logic into portable units;
- preserve multi-channel routing semantics;
- add DPF/VST3 wrapper code only after the core behavior is pinned down.

Current reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`

Implementation status:

- portable core effect engine now exists;
- parameter serialization now exists;
- deterministic tests now exist and run in CTest;
- no DPF wrapper yet.
