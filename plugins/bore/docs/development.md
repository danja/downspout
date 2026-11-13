# Building Bore: a reusable plugin-development path

Bore was designed as a small original instrument, rather than a port. The
development choices below are intended as a practical sequence for agents
building another plugin in this repository.

## 1. Choose a gap, then state the boundary

Before choosing DSP, compare the proposed sound source with the existing
instruments. Downspout already has strings and modal objects, wavetable and FM
synths, a tonewheel organ, birdsong and drum voices. Bore therefore models a
different excitation-and-resonance pair: nonlinear reed flow coupled to a
pitch-tuned air-column waveguide. It does not claim to model a specific trumpet,
trombone or player.

Write the boundary down early. Bore is an eight-voice MIDI instrument with
stereo output, no transport, samples, audio input, preset format or external
dependency. That kept the first version focused on one testable sound engine.

## 2. Define parameters as behavior

Each control was tied to a signal-path decision before the UI was drawn:

- Pressure changes the nonlinear reed drive and the feedback gain.
- Embouchure changes the lip oscillator frequency relative to the bore.
- Bell controls the feedback low-pass cutoff; Breath adds filtered noise.
- Reed Instability adds turbulent-flow noise and slow lip flutter.
- Register Capture feeds more AC reed flow into the bore; high pressure and
  tighter embouchure can let a nonlinear upper mode catch.
- Attack and Release shape a per-note envelope; Velocity controls sensitivity.
- Level and MIDI In are ordinary host parameters.

CC 1-6 follow the six tone/instability controls in panel order, and CC 7
controls Level. The status output is read-only. Parameters are ordinary host
state, so the wrapper does not invent a second serialization format.

## 3. Make the core portable and bounded

Keep the DSP independent of DPF. A note uses a fixed-capacity delay buffer,
linear fractional-delay interpolation and one-pole loop damping. MIDI events
are passed to the core with frame offsets, so it can render between events
without knowing about host classes. The engine has fixed voice storage and
performs no allocation in `process()`.

The loop delay is `sampleRate / frequency`, with note frequency bounded to
20 Hz-12 kHz and the delay bounded by the fixed buffer. A nonlinear reed
aperture gates Bernoulli-style airflow from the pressure difference between the
mouth and reflected bore wave. Reed Instability adds filtered turbulent flow
and slow aperture/frequency flutter. A damped reed-opening pole softens valve
closure; a DC blocker removes mean airflow before the acoustic component enters
the delay line, initialized on the first flow sample to avoid a note-on impulse.
Register Capture scales that AC flow by mouth pressure and Embouchure, so upper
modes can catch without directly transposing the MIDI note. Bell maps to a
low-pass cutoff from 900 Hz to the lesser of 15 kHz and 42% of sample rate.
These are sound-design approximations, not measured brass acoustics; describe
that distinction anywhere the UI or docs explain the model.

Attack reaches about 99% over its displayed time. Release falls by about 60 dB
over its displayed time, then the voice is retired below the -100 dB runtime
threshold. Activation clears runtime state but preserves parameters, matching
the host lifecycle contract.

## 4. Test sound, not private implementation

The core suite checks:

- exact MIDI event onset and invariance to where the host splits blocks;
- a sustained middle-C fundamental against a lower subharmonic using an
  independent direct DFT;
- note velocity, envelope release, controller mapping, channel filtering and
  all-sound-off behavior;
- audible effects of Reed Instability and Register Capture, including increased
  second-harmonic energy at high pressure/tighter Embouchure and no capture when
  mouth pressure is below the onset threshold or Embouchure is at its loose limit;
- that the tone controls change rendered audio, not just stored parameter
  values;
- finite, bounded output with eight voices at a high sample rate and maximum
  settings; and
- that sample-rate activation resets voices without resetting the patch.

These are deterministic core tests. They do not prove the instrument is
pleasant or that a DAW routes it correctly; label host audition as pending
until someone actually listens and checks save/reopen behavior in a host.

## 5. Keep the framework wrapper thin

The DPF class translates parameter metadata, host MIDI messages and sample-rate
callbacks. It does not contain DSP. A new VST3 target should use the existing
`dpf_add_plugin()` pattern and a plugin-local `DistrhoPluginInfo.h`; do not
modify shared framework code just to make one plugin fit.

For the UI, reuse the existing Magneto look-and-feel kit rather than duplicating
its slider and mouse handling. Draw the instrument's signal path first, then
place the controls where their role is apparent. A status parameter is useful
when the UI needs live processor state, but it should remain output-only.

## 6. Integrate and verify the whole product

Adding a core is not the same as adding a plugin. Register its build option and
subdirectory, local VST3 install rule, `install.sh` option, both release bundle
lists, release notes, screenshot target, product page, capability reference,
README entry and `profile.ttl`. Run `scripts/check-bundle-lists.sh` so the
hand-maintained package lists agree.

Configure before building a new CMake tree. Build the core test and
`<plugin>-vst3` target, run the focused test, capture the real interface with
`scripts/capture-plugin-screenshots.sh <plugin>`, inspect it at full resolution,
and only then publish the screenshot. Keep the plugin's host-audition status
explicit; a passing build is not host validation.
