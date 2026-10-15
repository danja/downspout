# Beards design notes

Source: `~/github/eurorack/beads` (Mutable Instruments Beads, MIT, Copyright 2017 Emilie
Gillet). Beards ports the granular signal path; it does not vendor `stmlib`, so the few
helpers used (soft clip, smooth step, semitone ratio, one-pole, SVF high-pass) are
rewritten in `src/beards_engine.cpp`.

## Ported faithfully

- Grain engine: 32 grains, voice stealing and the two scheduler modes, the
  attack/sustain/release grain envelope and its Shape mapping, equal-power pan, 22.10
  fixed-point read head with Hermite interpolation, stop-at-head policy with the
  256-sample safety fade, amplitude `1/sqrt(active+1)`.
- Grain duration law (reverse below a third), Position scaled to the recorded length.
- Trigger generator, internal-clock branches: steady pulse train below 48% density,
  randomised above 52%.
- Randomised parameters in the no-CV branch with a positive attenuverter, exposed as the
  four scatter controls.
- Feedback path: per-quality low-pass and soft-limit constants, input limiter, 40 Hz DC
  blocker, freeze with the 256-sample tail crossfade.
- Dry/wet law (post gains 0.75 / 1.5), pre-reverb and output limiters.

## Changed or assumed

- **Sample rate and storage.** Beads runs at 24-48 kHz with 8/12/16 bit packed storage.
  Beards runs at the host rate with a float buffer of 4 seconds and applies the
  quantisation on write: Digital none, Vintage 12 bit, Tape 8 bit mu-law. The dry signal
  is quantised the same way for Vintage and Tape, as in Beads. Tape does not drop the
  sample rate to 24 kHz and has no wow and flutter.
- **Pitch.** Beads' pitch knob has no published mapping in the DSP code; Beards exposes
  semitones directly (-24 to +24) and the scatter adds up to +/-24 st.
- **Reverb.** Beads' reverb engine (`fx_engine.h`) is not used. Beards has its own
  Griesinger/Dattorro tank with the same structure and delay lengths scaled to the host
  rate. It will not null against hardware.
- **Seed button and gate input.** Beards has no gate input. The clock free-runs from the
  first sample; the **Grain** button, CC 24 on the Conductor channel, and the Grain
  parameter's rising edge fire one grain at the start of the next block.
- **Transport sync (addition).** The `sync` parameter (Off, 1/1 to 1/32) fires a grain on
  every grid line of the host's BBT position (converted to quarter notes using the time
  signature denominator) while the transport is playing. The engine then free-runs from
  the block-start position at the host tempo and resyncs on start or a jump of more than
  0.05 quarter notes. A grid line exactly at the start position fires. While sync is
  active and the transport plays, the internal clock is muted so the pattern is the grid;
  stopped transport, or Off, uses the internal clock as before. The Grain button and
  Conductor Reset still fire on top.
- **Not ported:** the delay (multitap) mode, the CV inputs and attenuverter polarity
  (random-walk modulation, negative attenuverters), the divider mode driven by an
  external gate, the output-gate mode, mono buffer mode and the gain calibration.

## Host behaviour

- **State.** Every setting is an ordinary parameter and DPF saves those; the plugin has no
  DPF state. The audio buffer, grains and reverb are runtime state, cleared by
  `activate()` and `sampleRateChanged()`, which never touch the patch.
- **MIDI.** CCs are parsed in the portable core (`applyMidi`) and applied at the top of
  each block. The panel does not follow CC changes. Output status parameter
  `out_grains` feeds the grain counter in the header.
- **Latency.** None reported; the dry path is not delayed.
- **Level.** The processor can get loud with Feedback and Reverb high; the output limiter
  catches peaks above 0.9 but the user should trim.
