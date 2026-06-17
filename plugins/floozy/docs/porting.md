# Floozy porting notes

Floozy follows the current Downspout plugin pattern:

1. `FloozyEngine` owns the portable voice engine and MIDI handling.
2. `FloozyPlugin` maps DPF parameters, MIDI events, sample rate, and stereo
   outputs to the engine.
3. `FloozyUI` is a NanoVG editor over the same parameter table.
4. `downspout_floozy_core_tests` exercises core behavior without DPF or a DAW.

## Parameter contract

The parameter table lives in `include/floozy_params.hpp`. The symbols are
stable and trace the original LV2 control intent:

- source algorithm and source shaping;
- envelope attack/release and interface type/intensity;
- quantized body tuning, resonator ratio, and feedback;
- filter and modulation;
- reverb and master gain.

## Host mapping

The DPF wrapper exposes Floozy as an instrument:

- no audio inputs;
- two audio outputs grouped as stereo;
- one MIDI input;
- VST3 category `Instrument|Synth`;
- metadata maker `danja`, brand/group `Downspout`.

## Body stage

The LV2-era PM modules are used as historical reference only. The current
Downspout core uses a local two-resonator body model whose excitation and
resonator profile changes with the selected interface type:

- Hit/Drum emphasize short noise impulses and lower feedback.
- Reed/Flute/Brass use sustained pressure-style excitation against the body
  feedback.
- Pluck/Bow/Bell use transient, friction, or inharmonic profiles.

`Tune` remains a normalized host parameter for compatibility, but the engine
maps it to quantized semitone offsets from -24 to +24 before calculating body
delay lengths. The UI displays the snapped semitone value.

The body delay feedback controls use a curved response: low values damp the
string/body quickly, default values preserve the sustained interfaces, and high
values above the defaults push the delay loops close to self-sustaining, reduce
body damping, and raise the resonator contribution for a stronger
Karplus-Strong-style tail. Released voices stay alive long enough for that tail
to decay instead of being stopped as soon as the envelope reaches zero. Body
delay length, feedback, and cross-feedback changes are smoothed to avoid
single-sample jumps while host automation is moving, and tiny filter, body, and
reverb states are zeroed to avoid denormal CPU spikes in long tails.
