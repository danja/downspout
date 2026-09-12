---
title: Magneto
order: 159
bundle: magneto.vst3
kind: Instrument
role: Combustion engine generator
screenshot: /assets/plugins/magneto.png
capabilities: [audio output, MIDI CC input, host transport]
summary: Physically informed four-stroke engine — waveguide cylinders, intake runners, extractors, muffler and tailpipe — driven by RPM and throttle, with a selectable listening position.
---

## Opinion

The only thing in the set that makes a noise no synthesiser makes. Put it on
Rear with Silencing near zero and a long Straight pipe and you have a muscle car;
wind Growl up on eight cylinders and it lopes. It is also a fine drone source if
you ignore what it is modelling — automate RPM slowly under a pad and it reads as
machinery rather than music, which is exactly what some records need.

## Functionality

Magneto synthesises a combustion engine from its mechanics, not from samples. It
is a port of Baldan, Lachambre, Delle Monache and Boussard, *Physically informed
car engine sound synthesis for virtual and augmented environments* (SIVE'15).

A phasor running at `RPM/120` Hz — one ramp per engine cycle, which is two
crankshaft revolutions — drives four functions per cylinder: intake valve,
exhaust valve, piston motion and fuel ignition. Each cylinder is a bidirectional
digital waveguide: ignition is injected at the piston crown, the valves modulate
the reflection coefficients at the head, and the piston modulates the delay
length according to the compression ratio. Each cylinder feeds an intake runner
and an extractor; the extractors join a straight pipe, which feeds four
parallel muffler elements whose delays are snapped to prime sample counts so
their resonances never coincide, and then a tailpipe.

Three signals leave the model — the intake runners, engine-block vibration, and
the tailpipe — and the listening position mixes and places them.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Cylinders | 1–12 | 4 | Firing rate is cylinders × cycle rate |
| Displacement | 100–1200 cc | 500 cc | Per cylinder; sets chamber resonance |
| Compression | 6–14 :1 | 10 | Depth of the piston delay modulation |
| Ignition | 2–100 % | 15 % | Width of the explosion impulse |
| Growl | 0–100 % | 12 % | Uneven firing intervals — the V8 and big-twin lope |
| Intake runner | 0.05–1.50 m | 0.35 m | Average length; runners detuned ±10 % |
| Turbulence | 0–100 % | 60 % | Aspiration noise at the intake valve |
| Extractor | 0.10–1.50 m | 0.60 m | Head to manifold |
| Straight pipe | 0.20–4.00 m | 1.80 m | Manifold to muffler |
| Muffler | 0.10–1.50 m | 0.50 m | Average of four prime-length elements |
| Silencing | 0–100 % | 60 % | 0 is straight through, 100 is silent |
| Tailpipe | 0.05–1.00 m | 0.25 m | Outlet after the muffler |
| Backfire | 0–100 % | 20 % | Only fires on overrun; each one makes the next less likely |
| RPM | 400–9000 | 850 | Manual mode. CC 2 |
| Throttle | 0–100 % | 0 % | Ignition energy and block vibration. CC 1 / CC 11 |
| Speed source | Manual, Host Sync | Manual | Sync derives engine speed from host tempo |
| Sync ratio | 1–16 cycles/beat | 4 | Target RPM is 2 × bpm × ratio |
| Idle | 400–1500 rpm | 800 | Held when the transport stops |
| Inertia | 10–3000 ms | 400 ms | Flywheel |
| Seed | 1–9999 | 1 | Turbulence and backfire streams |
| MIDI Ctl | Off, 1–16, All | All | Controller channel filter; notes are ignored |
| Listen | Cabin, Front, Rear, Exterior | Cabin | Balance and stereo placement of the three sources |
| Intake / Block / Exhaust | 0–100 % | 50 / 35 / 80 % | Per-source levels |
| Width, Output | 0–100 % | 60 / 75 % | Stereo spread and output level |

### Status

Core DSP, deterministic tests, the VST3 target and the catalogue screenshot are
complete. Host validation in REAPER remains.
