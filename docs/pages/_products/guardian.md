---
title: Guardian
order: 119
bundle: guardian.vst3
kind: Audio effect
role: Output safety
screenshot: /assets/plugins/guardian.png
capabilities: [audio input, audio output, reported latency, diagnostics]
summary: DC removal, bounded look-ahead limiting, true-peak protection, silence detection, and latched fault recovery.
---

Guardian is intended for the end of autonomous graphs. It replaces non-finite
input with silence and exposes gain reduction, peak, overload, silence, and
fault status without audio-thread logging.
