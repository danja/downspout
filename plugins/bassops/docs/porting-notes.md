# bassops – design notes

## Overview

bassops is an original Downspout plugin (no LV2 source to port from). It combines two
common bass-focused processing techniques into a single 4-in/2-out effect:

1. **Sidechain ducker** – a kick-drum send on inputs 3/4 ducks the main signal on 1/2.
2. **Mid/side EQ** – after ducking, bass frequencies are kept centred (LP on mid) while
   high frequencies keep their stereo width (HP on side). One cutoff control links both
   filters.

## Signal flow

```
inputs 3-4 (sidechain)
    └─ peak envelope follower ─┐
                                ↓
inputs 1-2 (main) ──── VCA (duck gain) ──── M/S encode
                                                ├─ mid  ──── LP FIR ──┐
                                                └─ side ──── HP FIR ──┤
                                                              M/S decode ──── outputs 1-2
```

## FIR filter design

Both filters use a Hann-windowed sinc design of order 128 (129 taps, latency = 64 frames).
The LP and HP are a perfect complementary pair:

```
h_lp[n] = sinc(2·fc·(n − M/2)) · hann(n)
h_hp[n] = δ[n − M/2] − h_lp[n]
```

This gives `h_lp + h_hp = δ` (unit impulse), so when the cutoff is at DC or Nyquist the
decode output exactly equals the pre-encode signal.

The latency (64 frames) is reported to the host via `getLatencyInFrames()`.

## M/S matrix

Encode with /2 so passthrough (LP and HP as identity) gives unity gain:
```
mid  = (L + R) / 2
side = (L − R) / 2
L_out = LP(mid) + HP(side)
R_out = LP(mid) − HP(side)
```

## Parameters

| Parameter   | Range          | Default | Notes                                    |
|-------------|----------------|---------|------------------------------------------|
| Duck Depth  | 0–100 %        | 80 %    | 0 = no ducking, 100 = full silence on hit|
| Attack      | 1–500 ms       | 10 ms   | Envelope follower rise time              |
| Release     | 10–2000 ms     | 100 ms  | Envelope follower fall time              |
| M/S Cutoff  | 50–5000 Hz     | 200 Hz  | LP/HP crossover; typical: 80–300 Hz      |

## DPF mapping notes

- **NUM_INPUTS = 4**: inputs 0–1 are main stereo, inputs 2–3 are sidechain. Sidechain
  ports are placed in a custom port group (ID 100) named "Sidechain" via `initPortGroup`.
- **NUM_OUTPUTS = 2**: standard stereo output in the built-in stereo port group.
- **WANT_TIMEPOS = 0**: plugin is not transport-aware.
- **Latency**: 64 frames reported via `getLatencyInFrames()`; hosts should delay-compensate
  other tracks accordingly.

## Known limitations / future work

- The FIR coefficients are recomputed sample-accurately on cutoff change (per block).
  For very fast parameter automation this may introduce brief tonal artefacts; a
  cross-fade between old and new coefficients would eliminate them.
- The envelope follower uses peak detection on the louder sidechain channel; a true
  RMS option could be added as a future parameter.
- Attack/Release slider nudge in the UI uses a linear step for a log-scale parameter;
  a finer log step would be more ergonomic.
