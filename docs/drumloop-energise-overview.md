# Drum-loop energise overview

`energise` is a proposed drum-break re-quantisation and resynthesis tool. The
user gives it a low-quality, possibly unquantised drum break; it detects and
classifies the individual hits, maps them to a musical grid, then rebuilds the
break with cleaner transients and more controlled tone.

The differentiator should be the **editable event list**, not a black-box “AI
drum” button. Drum transcription is never perfect, so the tool has to make its
decisions visible and correctable.

## Product decision

Build an **offline capture → review → render** tool first, not a zero-latency
insert. Transcription, classification and high-quality resynthesis are much
easier without a real-time deadline, and the editor needs time to show the
event list anyway.

A practical host contract is:

1. capture or import a short audio region;
2. analyse it into a time-stamped drum-event list;
3. review and edit class, timing, velocity, ghost flag and confidence;
4. quantise with controllable strength and groove;
5. resynthesise into a new rendered region;
6. A/B the original and the render, with per-class mute/solo;
7. optionally export the detected events as MIDI to drive an external synth.

Because DPF does not expose a host time selection, the first capture path should
be a UI **Capture** action that records the next N seconds from the audio input,
with a plugin-local WAV import path added only if it can be done without a new
vendored dependency. The output is the plugin's rendered audio; automatic
replacement of a DAW media item is out of scope.

## Analysis pipeline

Keep the analysis as separate stages behind portable interfaces rather than one
large model.

### Onset detection

Use multi-resolution STFT or filterbank novelty:

- spectral flux plus phase deviation for transients;
- separate low, mid and high bands so a kick does not mask hats;
- sub-sample refinement around detected peaks;
- multi-label onsets, because kick+hat and snare+hat often coincide.

Timing errors are more audible than class errors, so onset quality and grid fit
deserve more effort than labels.

### Tempo and grid fitting

Estimate:

- tempo;
- grid phase;
- bar/beat origin;
- optional tempo drift.

Use onset-envelope autocorrelation plus a beat-tracking pass, then fit a
piecewise tempo map. Do not assume a perfectly constant grid: breaks often have
swing, drift and pushed phrases.

### Classification

For each onset, extract a short context window:

- band energy and decay shape;
- spectral centroid, flatness and rolloff;
- pitch contour for toms and kicks;
- noise ratio for snares and hats;
- stereo position and width;
- attack sharpness and tail length;
- velocity proxy.

Classes should be multi-label:

- kick;
- snare;
- clap/rim;
- closed hat;
- open hat;
- ride/crash/cymbal;
- low/mid/high tom;
- percussion;
- ghost flag;
- confidence and velocity.

A small CNN or transformer over log-mel patches would be reasonable later. For
the first version, use a classical feature plus rule/gradient-boosted-tree
classifier to validate the pipeline without a large dependency. Optional source
separation can improve features or provide cleaner reference hits, but is not
needed for the first pass.

## Quantisation model

Separate **macro** and **micro** timing:

- **macro:** bar/beat grid alignment;
- **micro:** human offset and groove feel.

Expose:

- grid: 16ths, 32nds, triplets, dotted values;
- quantise strength: 0–100 %;
- swing amount;
- preserve early/late feel;
- ghost-note strength, usually less quantisation;
- per-class timing offsets;
- humanise amount after quantisation;
- tempo-drift allowance.

The default should not be hard quantise. Something around 60–75 % strength with
ghost notes preserved will sound more musical.

## Resynthesis strategy

A hybrid will sound better than pure synthesis and is much cheaper than a large
neural codec:

- **Transient layer:** short click/noise burst, 1–3 ms attack, class-specific
  filtering.
- **Body layer:** tuned sine/FM/noise components.
  - kick: pitch-dropping sine + click + saturation;
  - snare: noise + tuned body + snap;
  - hat: filtered noise/metallic partials;
  - tom: pitched drop with noise attack;
  - cymbal: metallic partial bank + long stereo decay.
- **Residual/texture layer:** a filtered fragment of the original hit,
  time-snapped and crossfaded, to retain kit character.
- **Stereo/space:** per-class width, pan, tail length and room send.

Use oversampling for saturation and other aliasing-prone nonlinearities. Add
slight seeded per-hit variation so repeated hits do not sound machine-gunned.

For “cutting edge” resynthesis, a neural renderer can be added later as an
optional backend conditioned on class, velocity and a timbre embedding from the
original hit. It should not be the MVP: it creates build-size, licensing,
determinism and latency problems.

## UI design

Use the repo's Magneto-style panels, plus one novel editable component: a
waveform/event lane.

It should show:

- waveform;
- detected events as coloured markers by class;
- grid lines;
- arrows showing original → quantised position;
- confidence as opacity or bar height;
- velocity as marker size;
- ghost notes as hollow markers.

Interactions:

- click a marker to select it;
- drag timing;
- scroll wheel or keys to change class;
- shift-drag velocity;
- A/B original versus render;
- per-class mute/solo;
- undo/redo.

Use ordinary parameters for global settings and plugin state for the captured
audio and event map. Use output parameters for live status: detected count,
selected class, render progress and confidence.

## Testing and evaluation

Do not test only by listening. Build deterministic fixtures:

- synthetic break with exact kick/snare/hat/tom events;
- jittered timing;
- ghost notes;
- overlapping hits;
- cymbal tails;
- bit-crushed/noisy input.

Measure:

- onset precision/recall with a 20–50 ms tolerance;
- classification accuracy;
- tempo/grid error;
- quantisation error after strength is applied;
- transient energy and spectral contrast before/after resynthesis;
- no clipping and stable decay.

Then validate on real breaks; synthetic-only training will overfit badly.

## Suggested phases

1. portable data model: `DrumEvent`, `DrumClass`, `GrooveMap`,
   `RenderSettings`;
2. offline analyser: onset detection, tempo/grid, event list, MIDI export;
3. basic synth: class voices plus transient layer;
4. UI editor: waveform, event markers, manual correction;
5. state and A/B render: save events/settings, render replacement audio;
6. optional ML: ONNX classifier or neural residual renderer behind a clean
   interface;
7. host validation: REAPER, save/reopen, CPU, render determinism.

Keep all analysis and synthesis in a portable core, use DPF only as the shell,
and reuse the existing state/UI patterns rather than introducing a second
framework. Do not vendor a third-party model runtime until the classical core
pipeline is proven.
