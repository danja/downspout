# canticle design notes

Canticle is an original Downspout voice rather than an LV2 compatibility port.
It fills the ensemble role that was missing after `basilico`, `drumkit`,
`gremlin`, and `floozy`: a stable polyphonic chord and lead body.

Mapping decisions:

- MIDI input is one ordinary instrument input; note-on, note-off, all-sound-off,
  and all-notes-off are handled in the portable core.
- The core exposes 12 host parameters and relies on normal host parameter
  persistence for saved state.
- The five models are profile layers over one engine, not separate DSP graphs.
- Output is always stereo and bounded with a final soft clip so dense Cadence
  chords cannot emit non-finite or runaway audio.
