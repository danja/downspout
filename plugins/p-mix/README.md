# p-mix port

This directory contains the `downspout` port of `~/github/flues/lv2/p-mix`.

Current focus:

- validate the Reaper behavior of the transport-aware wrapper and UI;
- preserve multi-channel routing semantics under host testing;
- fold any host-specific UI/layout fixes back into the DPF front end;
- keep the wrapper thin while the release-build workflow is formalized.

Current reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`

Implementation status:

- portable core effect engine now exists;
- parameter serialization now exists;
- deterministic tests now exist and run in CTest;
- a first DPF/VST3 wrapper now exists;
- a first DPF/NanoVG UI now exists for the VST3 wrapper.

Recommended next steps:

1. validate the new `p_mix.vst3` UI in Reaper on stereo and multichannel tracks;
2. refine any host-specific layout or interaction issues from that validation pass;
3. make sure the wrapper installs cleanly in explicit `Release` builds as part of the release-build workflow;
4. add host-side notes once transport and UI behavior have been confirmed in practice.
