# melgen

`melgen` is a phrase-aware MIDI melody generator derived from the proven
`bassgen` transport, state, and DPF wrapper architecture.

The musical core is new:

- scale-degree melody generation rather than bassline generation;
- explicit phrase metadata in pattern state;
- period forms: free, `A A`, `A B`, `A A'`, call/answer, and `A B A`;
- contour control: wandering, flat, rising, falling, arch, inverted arch;
- answer transforms: related, same, transpose, invert, compress, expand;
- a `Structure` control that moves from stochastic line generation toward
  stricter phrase derivation and cadence behavior.
- a `Follow` control that lets incoming MIDI notes pull generated pitches
  toward another line, such as `bassgen`, without copying it exactly.
- a `Color` control that increases melodic tension through wider scalar
  movement, phrase mutation, chromatic approaches, and simple enclosures.
- a strict, Fugue-friendly region of the existing phrase controls: `Call Answer`
  period, high `Structure`, and low `Leap`/`Rest` produces a tonic subject and
  dominant answer without adding a separate genre selector.
- scale choices include minor, major, Dorian, Phrygian, Mixolydian, harmonic
  minor, Lydian, melodic minor, whole tone, altered, diminished, bebop,
  pentatonic, blues, Locrian, and Phrygian dominant colors.

Current status:

- portable core exists;
- deterministic core tests exist;
- DPF/VST3 MIDI-input and MIDI-output wrapper exists;
- custom NanoVG UI exists, with `Line Shape` controls separated from
  `Phrase Structure` selectors and action buttons;
- install script enables `melgen` by default.

UI notes:

- `Line Controls` contains root, register, range, leap, color, hold, accent,
  density, rest, structure, cadence, follow, vary, length, phrase length, and
  seed sliders.
- `Phrase Structure` contains scale, period, contour, answer, grid, and MIDI
  channel selectors plus `New`, `Notes`, and `Rhythm` action buttons.
- The default UI height is intentionally taller than `bassgen` because the
  melody surface has more shaping controls.

## Host compatibility

The DPF/VST3 wrapper exposes a silent stereo output bus for compatibility with
hosts that reject event-only plugins with no audio outputs. The portable core
remains MIDI-only, and the wrapper clears both output channels every block.
