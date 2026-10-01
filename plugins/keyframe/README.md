# keyframe

`keyframe.vst3` — extrema-sampling time stretch (stereo audio effect).

An implementation of **Keyframe Time Stretching via Extrema Sampling**
(Matthew Nielsen, DAFx26-26). The input is reduced to a set of timestamped
local extrema, and the spacing between them drives an overlap-add splice whose
crossfade length is decided by content rather than by a fixed window. Transients
stay coherent because the keyframes crowd together around them, which shortens
the splice; sustained passages get long splices. There is no transient detector
anywhere in the signal path. Full write-up in [docs/design.md](docs/design.md).

Insert on a stereo bus or an audio track. The default (unity rates, 60% wet) is
close to transparent.

## Using it

**Time** is a rate, not a stretch factor: `1.00x` is unity, `0.50x` is half
speed, so the output is twice as long. It stops at `1.00x` because a live input
cannot be read faster than it arrives.

**Pitch** is the playhead's read rate and is independent of Time. `2.00x` is an
octave up at the same duration; `0.50x` is an octave down. The pitch ratio is
Pitch ÷ Time, matching the paper's own parameterisation.

**Splice** is the leash length K in keyframes — the macro over splice duration.
Raising it lengthens the crossfade while leaving its adaptation to transients
intact. **Max Splice** caps how long a single crossfade may run, which is what
stops a long stretch through sparse material turning into a long audible repeat.

**Threshold** is the amplitude difference below which a candidate extremum is
discarded (-60 dB, the paper's recommendation). Raising it drops low-amplitude
detail, which reads as a low-pass because high frequencies tend to be quieter.

**Hold** freezes the reference playhead. The playhead keeps moving and each
splice pulls it back to the same point, so a passage sustains indefinitely.

**MIDI control.** CC 1 time, CC 2 pitch, CC 7 output, written through to host
parameters so automation and panel agree.

## Latency

Reported latency is a constant 8192 samples, set through `setLatency()`. The
dry path is delayed by the same amount, so dry and wet align and Mix does not
comb. Sustained slow-downs reach an internal depth limit of 65536 samples, past
which the engine loops the material it holds rather than backing up without
bound — the same audible-repeat behaviour the paper reports as a limitation of
the method.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_KEYFRAME=ON
cmake --build build --target downspout_keyframe_core_tests keyframe-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_keyframe_core_tests --output-on-failure
```

Core tests cover parameter clamping, silence stability, sparsification density,
keyframe ordering, unity transparency and its latency offset, dry alignment,
pitch up and down, the duration-stretch depth limit, hold, block-size
invariance, boundedness over ten seconds at extreme settings, status reporting,
and state round trip.

## Status

Core DSP, deterministic tests and the VST3 target are complete. Host validation
in REAPER is pending.