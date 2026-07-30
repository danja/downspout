# canticle design notes

Canticle is an original Downspout voice rather than an LV2 compatibility port.
It fills the ensemble role that was missing after `basilico`, `drumkit`,
`gremlin`, and `floozy`: a stable polyphonic chord and lead body.

Mapping decisions:

- MIDI input is one ordinary instrument input; note-on, note-off, all-sound-off,
  and all-notes-off are handled in the portable core.
- The core exposes host parameters and relies on normal host parameter
  persistence for saved state.
- The five models are profile layers over one engine, not separate DSP graphs.
- `Metal` is a model-independent edge control that adds inharmonic upper
  partials, extra brightness, and a little drive while preserving bounded
  output.
- The UI's right column is a dropdown role surface: `Model`, `Articulation`,
  `Register`, and `Ensemble` are real host parameters, so their choices persist
  and can be automated.
- Output is always stereo and bounded with a final soft clip so dense Cadence
  chords cannot emit non-finite or runaway audio.

Performance can be measured without a host using the
`downspout_canticle_processing_benchmark` target. It renders deterministic
48 kHz/1024-frame workloads for every model at 1, 6, and 12 active voices and
reports average and worst block times, followed by two simultaneous Glass-model
instances at representative and stress polyphony. Timing is diagnostic rather
than a pass/fail test because scheduler and CPU conditions vary between
machines.

Envelope curves, model profiles, register/detune ratios, voice cutoff, width,
and pan bases are derived on note or parameter changes rather than for every
voice on every sample. The filter coefficient is likewise recalculated only
when its cutoff or sample rate changes. The sample-processing path remains free
of allocation and locking.
