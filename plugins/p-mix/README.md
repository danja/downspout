# p-mix port

This directory contains the `downspout` port of `~/github/flues/lv2/p-mix`.

Current focus:

- validate the Reaper behavior of the transport-aware wrapper and UI;
- keep the public VST3 wrapper predictable on normal stereo tracks;
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

1. validate the stereo `p_mix.vst3` wrapper and the new manual mute toggle in Reaper;
2. refine any host-specific layout or interaction issues from that validation pass;
3. decide later whether a separate multichannel wrapper is worth keeping for DAWs that benefit from it;
4. add host-side notes once transport and UI behavior have been confirmed in practice.
