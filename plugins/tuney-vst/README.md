# Tuney VST

Tuney VST turns focused typing or stored text into stereo synthesized audio and
MIDI. The portable core ports the musical behavior of Tuney 0.3.39; the DPF
layer supplies the VST3 host and NanoVG interface. An optional Host Sync mode
runs stored-text note and rest timing from the host's BBT transport.

Build with `DOWNSPOUT_BUILD_TUNEY_VST=ON`. The bundle is `tuney_vst.vst3`.

Tuney is MIT-licensed, copyright Tom Ritchford. See `docs/porting.md` for the
behavior mapping and v1 boundaries, and `docs/TUNEY-LICENSE.md` for the source
license notice.
