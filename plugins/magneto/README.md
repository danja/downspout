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
soon as it is loaded — an engine idling at 850 rpm.

**Speed and load.** RPM and Throttle are ordinary automatable parameters. Set
**Speed source** to *Host Sync* to lock the engine cycle to host tempo instead,
choosing how many engine cycles fall on each beat; the engine drops to **Idle**
whenever the transport stops. **Inertia** is the flywheel: how quickly the
engine reaches a new speed.

**Playing it.** A MIDI note sets crankshaft speed rather than gating a voice.
The note is transposed down two octaves first, so the usable range sits
comfortably on the keyboard: C1 idles near 980 rpm, C2 cruises at about 1960,
and C4 hits the 9000 rpm limit. With four cylinders the firing rate lands on the
pitch actually played, because firing rate = cylinders x engine cycle rate, so
it can be played as a very rough bass instrument. Note velocity sets Throttle on
the same scale as CC 1, so a soft note idles at that speed and a hard one is
under full load. Note-off is ignored — the engine holds the last speed and load
it was given and Inertia does the rest. Notes still set Throttle while **Speed
source** is *Host Sync*, but not RPM, which Sync owns.

**MIDI control.** Notes and controllers are both gated by **MIDI in**
(off, a single channel, or all).

| CC | Control |
|----|---------|
| 1 (mod wheel) | Throttle |
| 2 (breath) | RPM across its full range |
| 3 | Silencing |
| 4 | Growl |
| 5 | Straight pipe |
| 6 | Turbulence |
| 7 | Output |
| 11 (expression) | Throttle |

CC 1-4 are the four most audible controls on purpose: those are the numbers
[Drift](../drift/) sends from its four lanes by default, so routing Drift's MIDI
into Magneto modulates the engine with no configuration. Point Drift's lane CCs
at 5, 6 or 7 to reach the rest.

A controller moves the engine but not the on-screen slider — DPF has no
DSP-side path back to the panel. The tachometer reads the processor's live
speed, so it stays truthful under MIDI or tempo control.

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
end of an autonomous chain. Good before it: Drift, for hands-off modulation.

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
absence of allocation after `activate`, muffler silencing, that the four
listening positions really do render different balances, the note-to-RPM
mapping, and that each of Drift's four default CCs reaches a parameter which
audibly changes the engine.

## Status

Core DSP, deterministic tests, the VST3 target and the catalogue screenshot are
complete. Host validation in REAPER is pending; the panel has been reviewed
against the repository's UI screenshot criteria and revised twice (a clipped
Drive row, a firing diagram that never rendered, and two captions running off
their panels).
