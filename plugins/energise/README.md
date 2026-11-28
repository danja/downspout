# Energise

Energise is an exploratory offline drum-break re-quantiser and resynthesiser. It
arms 1–4 whole bars (default 2) from the next host bar line on stereo inputs 1/2,
or loads a built-in six-second demo break, then detects onsets on the DAW's BPM
grid and labels each hit with a portable rule classifier. Capture is hard-capped
at 8 seconds, so slow tempos use fewer bars; the independent tempo/drift fitter
is retained only as a swappable offline utility.

The workflow is **Demo/Capture → Analyse → Edit → Render → A/B**. After
analysis the waveform panel shows source hits and their quantised targets with
arrows; the table shows time, class, velocity, confidence, quantised time and
ghost status. Click a marker or row to select an event, then use the edit strip
or keys `A`/`D` (class), `W`/`S` (velocity), `G` (ghost) and `[`/`]` (page). Edits
are ordinary parameter values committed by the **Edit Event** counter, so VST3
hosts forward them reliably.

The renderer rebuilds each hit from a transient, a tuned drum body and a
filtered residual of the original audio. Quantise, Ghosts, Swing, Humanise and
the voice levels shape the result. **Audio Out** selects Off (default), Pass
(monitor the inputs) or Render (loop the captured/rendered buffer); **A/B** swaps
which buffer Render loops. **MIDI Out** gates channel-10 GM drum notes and
**BBT Loop** locks both audio and MIDI loop phase to the host bars; with it off,
the loop free-runs. Neither replaces the host's input or media items.

The project is stored as versioned text state. Controls, status, waveform peaks
and the visible event-list shadow are host parameters, so the UI can show live
processor state without a DSP→UI callback. There is no ML model, WAV import or
real-time triggered playback in this first pass.

![Energise interface](../../docs/pages/assets/plugins/energise.png)

See `docs/energise-plan.md`, `docs/energise-progress.md` and
`docs/drumloop-energise-overview.md` for the plan, phase status and design
background.
