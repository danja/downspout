# NAM design notes

## Scope

A player, not a trainer: it loads existing `.nam` models and WAV impulse responses. Behaviour of the
network is entirely NeuralAmpModelerCore's; this plugin adds the host plumbing, a convolver, resampling,
and the strip around the model.

## Vendored code

`third_party/NeuralAmpModelerCore` is the `NAM/` directory of sdatkinson/NeuralAmpModelerCore at commit
`0b3d3c9` (v0.6.0 plus WAV-as-Linear loading), unmodified, with its MIT licence. Eigen 3.4.0 (MPL2) provides
the headers it needs: `Eigen/` and `unsupported/Eigen/FFT` only. `third_party/nlohmann/json.hpp` (already in the
repo) serves NAM's `#include "json.hpp"`. Defined: `NAM_ENABLE_A2_FAST` (as upstream); `NAM_SAMPLE` stays double.

The vendor library is built without the project warning flags and linked as a whole archive:
each architecture registers itself from a static initializer in its own object file, and a normal static link
drops those objects, after which every model fails with "No config parser registered". This needs CMake 3.24
(`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`); older versions skip the plugin with a warning.

## Threading and state

- `setState` (host main thread) loads the file, builds a rig, and publishes it with an atomic shared-pointer swap.
  `run()` takes the pointer once per block. A superseded rig is parked in a graveyard until the audio thread has
  let go, so the audio thread never frees a network.
- `activate()` calls `Engine::reset()`: filters, gate, meters and convolver history only. The loaded files and
  parameters survive (`wrapper_probe.cpp` checks this through the real DPF wrapper).
- A sample-rate change reloads both files for the new rate. State uses `WANT_STATE` with `WANT_FULL_STATE`
  (`scripts/check-plugin-state.sh` is clean).

## Decisions

- **Model rate.** If the file declares a training rate and the model cannot run at an arbitrary rate, the network
  runs at that rate and the audio is resampled around it. Unknown rates, or ratios beyond 8:1, run at the host rate.
- **Resampler latency.** The resamplers need their look-ahead before the first sample exists. The output FIFO is
  pre-filled with exactly that many zeros (plus a 4-sample margin), which is the reported latency; the dry path is
  delayed by the same amount. A host-block-size-independence test and an underrun counter guard this.
- **Loudness normalisation** targets -18 dB using the model's own `loudness` metadata, when present.
- **IR resampling** keeps the kernel's pre-ringing, so a sharp onset keeps its level; leading samples are dropped
  only while they carry less than -60 dB of the IR's energy, so a typical cabinet IR is not delayed.
- **Stereo.** Two network instances are always loaded (the second is idle in Mono sum mode): a few MB for a
  standard WaveNet, and a switch without a reload.
- **Safety.** Non-finite samples become 0 and the output is clamped to +-8 (+18 dBFS). Denormals are flushed to zero
  for the duration of each block.
- **Tone** is placed after the model, as in a guitar amp's tone stack position for captured "amp only" models;
  models that already include a tone stack are unaffected at 0 dB.

## Tests (`tests/nam_core_tests.cpp`)

Convolver against a direct computation for IR lengths 1 to 5000 and random block sizes; resampler passband gain
(1 %) and rejection above the output Nyquist (>44 dB); biquads and the tone curve against their analytic
responses; empty-engine pass-through; gains and clamp; gate; both example models load, are not pass-throughs and
give identical samples at any block size; loudness normalisation measured against the model's metadata; the
resampled model path (latency bounds, block-size independence, no underruns, dry delay alignment); IR loading
(level, 44.1 kHz IR at 48 kHz, 2 s trim, stereo channel mapping); every non-output parameter changes the output
(a new parameter without an entry fails); outputs are read-only; meters.

The suite also passes under ASan and UBSan (the plugin sources instrumented, the vendored NAM not).

## Not done

All nine of the core's example models load and run (WaveNet A1 and A2, LSTM, slimmable containers, the A2 family).
Measured with a 220 Hz tone at 48 kHz, Release, one core, a lightly loaded machine, Mono sum (so one network):
the standard A1 WaveNet (`wavenet_a1_standard.nam`) takes about 37 % of a core, the A2 example 11 %, the small
WaveNet and LSTM examples 1-2 %. Stereo runs two networks, so a standard WaveNet there is about 75 % of a core.
These are upper bounds (the machine was not idle); a host with several instances needs the A2-sized models.
There is no oversampling, no slimmable-model size control, and no model-switching crossfade (a new file replaces the
old one at a block boundary). Not yet auditioned in a host with real captures.
