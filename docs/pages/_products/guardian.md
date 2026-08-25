---
title: Guardian
order: 119
bundle: guardian.vst3
kind: Audio effect
role: Output safety
screenshot: /assets/plugins/guardian.png
capabilities: [audio input, audio output, bypass, input gain, attack/release, variable clipper, reported latency, diagnostics]
summary: Input gain, look-ahead limiting with attack and release, variable-curve clipping (soft to hard), DC removal, true-peak protection, bypass, and latched fault recovery.
---

## Opinion

Still testing, but so far seems a good alternative to pre-existing limiters and clippers.

## Functionality

Guardian is intended for the end of autonomous graphs. An input gain control
drives the limiter harder or preserves headroom. The clipper shape parameter
sweeps continuously from no clipping through gentle tanh saturation to a
near-hard clip. A transfer-curve graph shows the full input-to-output response
in real time. Bypass, overload, silence, and fault diagnostics complete the
safety picture.
