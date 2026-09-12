# Magneto

`magneto.vst3` — physically informed combustion engine sound generator.

Magneto synthesises a four-stroke engine from its mechanics rather than from
samples. A phasor at `RPM/120` Hz drives intake-valve, exhaust-valve, piston and
ignition functions; each cylinder is a digital waveguide whose feedback is
modulated by its valves and whose length is modulated by its piston; the
cylinders feed intake runners and extractors, and the extractors feed a straight
pipe, a four-element muffler and a tailpipe. Three signals come out — intake,
engine-block vibration, and the tailpipe — mixed to stereo for a chosen
listening position.

It is ported from Baldan, Lachambre, Delle Monache and Boussard,
*Physically informed car engine sound synthesis for virtual and augmented
environments*, SIVE'15. Porting decisions and every assumption made where the
paper is underspecified are in [docs/design.md](docs/design.md).

## Using it

Magneto is an instrument: no audio input, stereo output, and it makes sound as
soon as it is loaded — an engine idling at 850 rpm. It ignores MIDI notes.

**Speed and load.** RPM and Throttle are ordinary automatable parameters. Set
**Speed source** to *Host Sync* to lock the engine cycle to host tempo instead,
choosing how many engine cycles fall on each beat; the engine drops to **Idle**
whenever the transport stops. **Inertia** is the flywheel: how quickly the
engine reaches a new speed.

**MIDI control.** With **MIDI control channel** set to a channel or *All*:

| CC | Control |
|----|---------|
| 1 (mod wheel) | Throttle |
| 2 (breath) | RPM across its full range |
| 7 | Output |
| 11 (expression) | Throttle |

A CC moves the engine but not the on-screen slider — DPF has no DSP-side path
back to the panel. The tachometer reads the processor's live speed, so it stays
truthful under MIDI or tempo control.

**Character.** Cylinders, Displacement and Compression set the size and pitch of
the machine; Ignition sets how abrupt each explosion is; **Growl** skews the
firing intervals, which is what gives cross-plane V8s and big twins their lope.
The firing diagram under those controls shows the resulting firing order.

**Pipes.** Intake runner, Extractor, Straight pipe, Muffler and Tailpipe are
lengths in metres, each shown with its tube resonance. **Silencing** is the
muffler's action — 0 is straight-through and loud, 1 is completely silent.
**Backfire** only fires while the engine is slowing down, and each backfire makes
the next one less likely.

**Listening position.** Cabin, Front, Rear or Exterior set the balance and the
stereo placement of the three sources. Rear is exhaust-led and dark, Front is
intake-led and bright, Cabin is dominated by block vibration.

Good next in the chain: Lightverb or Ambo for exterior space, Guardian at the
end of an autonomous chain.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_MAGNETO=ON
cmake --build build --target downspout_magneto_core_tests magneto-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_magneto_core_tests --output-on-failure
```

The core tests cover the phasor frequency, cycle-function boundaries, firing
rate against cylinder count, output finiteness under extreme and rapidly
modulated parameters, ring-down when the engine is silenced, backfire
determinism and its overrun-only condition, transport sync and idle hold,
parameter clamping, state round trip, identical output across block sizes,
absence of allocation after `activate`, muffler silencing, and that the four
listening positions really do render different balances.

## Status

Core DSP, tests and the VST3 target are complete and green. Panel screenshot
capture and host validation in REAPER are pending.
