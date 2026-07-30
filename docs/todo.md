
## Generative Audio Workstation plugin proposals

These are proposed Downspout VST3 plugins for constructing autonomous,
interacting music systems. Unless a proposal says otherwise, each plugin should
follow host transport, offer a deterministic seed, serialize all generative
state needed for repeatable playback, and expose useful capability metadata for
Transmission's RDF catalogue. Audio processing must remain allocation-free and
must not perform file I/O or take unbounded locks.

### Priority 1: composition and interaction

- [x] **Harmonic Atlas — autonomous harmony generator.** Produce chord tones,
  roots/bass guidance, and optional scale notes as MIDI using selectable tonal,
  modal, chromatic-mediant, and neo-Riemannian movement. Parameters should
  include harmonic rhythm, tension, cadence frequency, inversion range, voice
  count, and voice-leading strictness. It should generate useful progressions
  without MIDI input, but optionally follow incoming roots or pitch classes.
  Verify deterministic output, stuck-note cleanup, looping, tempo changes, and
  bounded polyphony.

- [x] **Conductor — long-form structure generator.** Arrange bar-aligned
  sections such as intro, development, break, reprise, and coda, emitting
  configurable MIDI notes and CC values that other generators can treat as
  scene, density, energy, mutation, and reset commands. Provide both a
  stochastic mode and a user-defined weighted section graph with minimum and
  maximum durations. Never mutate the host graph directly. Verify repeatable
  multi-minute forms, seek/reset behaviour, and clean transitions at section
  boundaries.

- [x] **Drift — general-purpose generative MIDI modulator.** Emit transport-
  synchronized MIDI CC from several independent lanes supporting LFO,
  sample-and-hold, bounded random walk, Lorenz-like chaos, and envelope
  following. Each lane needs range, polarity, smoothing, rate/division,
  phase, destination channel/controller, and seed controls. Rate-limit output
  so dense automation cannot overwhelm the host's bounded MIDI queues.

- [x] **Mnemosyne — motif memory and recombination processor.** Capture
  incoming MIDI phrases into a fixed-capacity reservoir, then recall, splice,
  transpose, invert, rotate, stretch, and probabilistically recombine them.
  Include distinct listen, accompany, and autonomous modes, plus controls for
  phrase length, novelty, continuity, register, and rhythmic fidelity. Verify
  bounded storage, deterministic recall, note-off correctness, and useful
  behaviour when the reservoir is empty.

- [x] **Polymeter — multi-lane rhythm and event generator.** Generate pitched
  or percussion MIDI using Euclidean patterns, independent lane lengths,
  rotations, probability, ratchets, accents, and controlled phase drift.
  Lanes should share a single MIDI output until Transmission supports routing
  individual VST3 event buses, using configurable channels and note ranges for
  separation. Verify that long coprime patterns remain sample-accurate and
  repeat exactly after transport reset.

- [x] **Oracle — audio/MIDI listener and response generator.** Analyze incoming
  audio for bounded, real-time features such as onset, level, pitch class,
  brightness, and density, and analyze MIDI for register, activity, and tonal
  centre. Map those observations to smoothed MIDI CC or constrained response
  notes so a patch can listen to and influence itself. Analysis windows and
  feature buffers must be preallocated; verify silence, noise, overload, and
  feedback-loop behaviour.

### Priority 2: generative sound and transformation

- [x] **Mosaic — generative sampler instrument.** Load a bounded sample pool on
  the control thread and produce MIDI-triggered or autonomous slices, grains,
  round robins, and layered variations. Include reproducible selection,
  transient-safe envelopes, pitch/register constraints, density, slice size,
  reverse probability, and stereo spread. Missing samples must fail silently
  with a visible status parameter rather than blocking audio processing.

- [x] **Resonance Garden — MIDI-tuned resonator bank.** Turn arbitrary audio
  into pitched, evolving material using a bounded bank of damped/modal
  resonators tuned by held MIDI notes or an internal scale. Provide decay,
  excitation, inharmonicity, damping, voice stealing, freeze, wet/dry, and
  feedback safety controls. Verify silence stability, denormal handling,
  bounded gain, and clean note replacement.

- [x] **Orbit — generative spatial motion effect.** Move stereo material using
  transport-aware trajectories, random walks, pendulum/orbit paths, mid/side
  width, distance filtering, and optional Doppler within conservative limits.
  Trajectories must be seedable and smoothly interpolated. Describe the plugin
  as binaural only if a real HRTF implementation is included.

### Priority 3: autonomous-system safety

- [x] **Guardian — output safety and diagnostics effect.** Provide DC removal,
  a bounded look-ahead limiter, true-peak protection, optional soft clipping,
  silence detection, and latched overload/NaN status parameters. It should
  recover safely from non-finite plugin output and expose gain reduction and
  fault counts without logging from the audio thread. Verify latency reporting,
  reset recovery, sustained overload, and non-finite input handling.
