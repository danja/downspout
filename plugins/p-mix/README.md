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
- a first DPF/VST3 wrapper now exists;
- the first wrapper currently ships without a custom UI.

Recommended next steps:

1. validate `p_mix.vst3` in Reaper on stereo and multichannel tracks;
2. decide whether the first wrapper should remain no-UI or gain a minimal control panel;
3. make sure the wrapper installs cleanly in explicit `Release` builds as part of the release-build workflow;
4. add host-side notes once transport behavior has been confirmed in practice.
