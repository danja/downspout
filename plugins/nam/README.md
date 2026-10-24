# NAM

Neural Amp Modeler player. Loads a `.nam` model (an amp, pedal or whole rig captured by the
[NAM trainer](https://github.com/sdatkinson/neural-amp-modeler)) and a `.wav` cabinet impulse
response, and runs them as an insert: input gain, noise gate, model, three-band tone, cabinet,
output. The model runs in Steven Atkinson's
[NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT), vendored in
`third_party/`; the cabinet is Downspout's own zero-latency partitioned FFT convolver.

Signal flow: `input -> input gain -> noise gate -> model -> loudness -> bass / mid / treble ->
cabinet IR -> low / high cut -> IR gain -> dry/wet -> output gain`.

| Control | Range | Default | Notes |
|---|---|---|---|
| Input gain | -24 to +24 dB | 0 | Into the gate and model: the drive control for most captures |
| Noise gate | -90 to -20 dB | Off (-90) | Before the model, so it silences hum between notes and not the model's own tail |
| Bass / Mid / Treble | -12 to +12 dB | 0 | 120 Hz shelf, 750 Hz peak, 3.5 kHz shelf, after the model |
| IR low cut | 20-500 Hz | Off (20) | Second-order high-pass after the cabinet |
| IR high cut | 2-20 kHz | Off (20k) | Second-order low-pass after the cabinet |
| IR gain | -24 to +12 dB | 0 | Cabinet level (IRs are not normalised) |
| Output gain | -24 to +24 dB | 0 | Applied to the processed signal only |
| Mix | 0-100 % | 100 | Dry is delayed to match the model's resampler |
| Channels | Mono sum / Stereo | Mono sum | Mono sum folds L+R into one chain; Stereo runs one model per side |
| Model on, IR on | Bypass / Active | Active | |
| Normalise model | Raw / -18 dB | -18 dB | Only for models that report a loudness (most recent ones do) |

Read-only outputs for the panel: input and output level, model and IR status, model rate, IR length.

## Files

- **Model**: any `.nam` with one input and one output channel. Architectures supported are those of the
  vendored core (WaveNet, LSTM, ConvNet, Linear, slimmable containers; the A2 family). A model
  trained at a rate other than the host's is resampled (windowed sinc, 32-256 taps) around the network;
  this adds latency, which is reported to the host.
- **IR**: mono or stereo WAV (16/24/32-bit PCM or float). Resampled to the host rate, trimmed to 2 s (with a
  10 ms fade), played through a partitioned FFT convolver whose first 128 taps run in the time domain, so the
  convolution adds no latency. A mono IR is applied to both channels; a stereo IR uses its left and right.
- Paths, not contents, are saved with the session (plugin state `model_file`, `ir_file`).
  Files are read on the host's main thread and handed to the audio thread as shared objects; the
  audio thread never loads, allocates or frees.

See `docs/design.md` for the assumptions and what the tests hold.
