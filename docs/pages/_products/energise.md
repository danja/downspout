---
title: Energise
order: 251
bundle: energise.vst3
kind: Audio effect
category: processor
group: rhythm
role: Drum-break re-quantiser
screenshot: /assets/plugins/energise.png
summary: Offline drum-break re-quantiser: capture whole bars on the host BBT grid or load a demo, edit detected hits, resynthesise and loop the break with audio/MIDI output switches.
---

## Opinion

A rough break often has the right hits but the wrong placement and weak
transients. Energise treats it as data rather than a waveform to process: it
lists every hit, lets you correct its class, time, velocity and ghost flag, then
rebuilds the loop from clearer components.

## Functionality

Press **Demo** for a built-in six-second break or arm **Capture** for 1–4 bars
from the next host bar line on inputs 1/2. Capture timing and BPM come from the
DAW transport; a hard 8 s state cap may shorten long bars. **Analyse** runs onset
detection and a rule/feature drum classifier on that locked grid. The waveform
shows source hits and quantised targets with arrows; the event table shows class,
velocity, confidence, quantised time and ghost status. Click a marker or row,
then edit with the strip or `A`/`D`, `W`/`S`, `G` and `[`/`]`. Edits commit through
the **Edit Event** counter so VST3 hosts forward them.

**Render** rebuilds the break from transient, tuned body and filtered residual
layers. Quantise, Ghosts, Swing, Humanise, voice levels, Saturation, Variation
and Output shape the result; the independent tempo/drift fitter is not used by
the processor. **Audio Out** selects Off (default), Pass (monitor inputs) or
Render (loop the buffer). **A/B** swaps which buffer Render loops. **MIDI Out**
gates channel-10 events and **BBT Loop** locks both audio and MIDI loop phase to
host bars; with it off the loop free-runs. Mute/solo masks apply to the render
and MIDI export. This is not a host-replacement insert.

This first pass is portable and deterministic: no ML runtime, model, training
data or WAV import. Classifier and voice implementations remain swappable. The
project is saved as versioned text state; live status, waveform peaks and the
visible event shadow are output parameters.

### Status

Core tests cover the demo fixture, onset recall/precision, classification
accuracy, host-locked analysis, BBT capture math, output-switch semantics,
quantisation, renderer tails and stability, event edits, undo/redo, MIDI export
and serialization. The VST3 target builds. The screenshot is stale after the
output-switch/UI revision; a fresh capture, live host transport/listening and
final visual acceptance remain.
