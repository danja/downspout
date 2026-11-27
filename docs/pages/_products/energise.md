---
title: Energise
order: 251
bundle: energise.vst3
kind: Audio effect
category: processor
group: rhythm
role: Drum-break re-quantiser
screenshot: /assets/plugins/energise.png
summary: Offline drum-break re-quantiser: capture 1-8 s or load a demo, detect and edit hits, fit a groove grid, resynthesise the break and A/B the result.
---

## Opinion

A rough break often has the right hits but the wrong placement and weak
transients. Energise treats it as data rather than a waveform to process: it
lists every hit, lets you correct its class, time, velocity and ghost flag, then
rebuilds the loop from clearer components.

## Functionality

Press **Demo** for a built-in six-second break or arm **Capture** for 1–8
seconds from inputs 1/2. **Analyse** runs onset detection, a tempo/grid fit and
a rule/feature drum classifier. The waveform shows source hits and quantised
targets with arrows; the event table shows class, velocity, confidence,
quantised time and ghost status. Click a marker or row, then edit with the strip
or `A`/`D`, `W`/`S`, `G` and `[`/`]`. Edits commit through the **Edit Event**
counter so VST3 hosts forward them.

**Render** rebuilds the break from transient, tuned body and filtered residual
layers. Quantise, Ghosts, Swing, Humanise, Drift, voice levels, Saturation,
Variation and Output shape the result. Mute/solo masks apply to both the render
and the channel-10 MIDI export. **A/B** swaps monitoring between the captured
and rendered buffers; it is not a host-replacement insert.

This first pass is portable and deterministic: no ML runtime, model, training
data or WAV import. Classifier and voice implementations remain swappable. The
project is saved as versioned text state; live status, waveform peaks and the
visible event shadow are output parameters.

### Status

Core tests cover the demo fixture, onset recall/precision, tempo/grid fitting,
classification accuracy, quantisation, renderer tails and stability, event
edits, undo/redo, MIDI export and serialization. The VST3 target builds and a
standalone screenshot has been captured; live host listening and final visual
acceptance remain.
