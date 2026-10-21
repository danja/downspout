# bassops – design notes

## Overview

bassops is an original Downspout plugin (no LV2 source to port from). It combines two
common bass-focused processing techniques into a single 4-in/2-out effect:

1. **Sidechain ducker** – a kick-drum send on inputs 3/4 ducks the main signal on 1/2.
2. **Mono split + stereo synthesis** – after ducking, the stereo input is summed to mono
   (a silent main L or R, i.e. a mono track, is treated as a copy of the other side).
   The delayed full-range mono is the mid. A waveshaper distorts the mono, and the
   harmonics it adds, high-passed above the cutoff, form a synthetic side. M/S decoding
   creates stereo width above the cutoff entirely from the distortion; the bass stays
   mono and clean. With side shape at zero the side is exactly zero and both outputs
   are the delayed mono.

## Signal flow

```
inputs 3-4 (sidechain)
    └─ peak envelope follower ─┐
                                ↓
inputs 1-2 (main) ──── VCA (duck gain)
                                └─ sum to mono ──┬─ 64-sample delay (mid) ────────────────┐
                                                 └─ [waveshaper] − mono ── HP FIR ─────┐  │
                                                                                         ↓  ↓
                                                                                   M/S decode
                                                                                         │
                              dry (64-sample delayed) ◄─── wet/dry mix ◄────────────────┘
                                                                              outputs 1-2
```

M/S decode:
```
outL = wetScale × (midF + sideF) + dryScale × delayedInL
outR = wetScale × (midF − sideF) + dryScale × delayedInR
```

At `sideShape=0%`: the side is zero, so both outputs are the delayed mono — stereo width
comes entirely from the dry wet/mix path.
At `sideShape>0%`: side = HP(shaped − mono), the harmonics the waveshaper adds above the
cutoff; they appear in anti-phase in L and R, so they cancel when the output is summed
to mono and the bass is untouched.

Note: original stereo difference of the main input is not preserved in the wet path;
the dry mix is the only route for retaining the source's stereo content.

## FIR filter design

The side high-pass is a Hann-windowed sinc design of order 128 (129 taps, latency = 64
frames), built as the complement of a unity-DC-gain LP kernel (the kernel is normalised;
an un-normalised 129-tap sinc at a low cutoff sums to about 0.5). With only 129 taps the
cutoff is soft at low settings (transition of order 1 kHz): it sets where the added
harmonics start, it is not a sharp crossover.

```
h_lp[n] = sinc(2·fc·(n − M/2)) · hann(n)
h_hp[n] = δ[n − M/2] − h_lp[n]
```



The latency (64 frames) is reported to the host via `getLatencyInFrames()`.

## Side-channel distortion

The waveshaper runs on the mono signal before it enters the HP convolution buffer.
Two coefficients are precomputed per block from the `sideShape` parameter (0–100 %,
normalised to 0–1 as `s`):

| Coefficient | Formula | Meaning |
|-------------|---------|---------|
| `drive`     | `exp(s × ln 8)` | 1 → 8; scales input before clipping |
| `hardMix`   | `s²`            | 0 → 1; blends tanh saturation toward hard clipping |

```
driven  = x × drive
soft    = tanh(driven)
hard    = clamp(driven, −1, 1)
clipped = soft + hardMix × (hard − soft)
output  = clipped / drive          ← unity small-signal gain
```

At `sideShape=0%`: `drive=1, hardMix=0` → no distortion, straight through.
At `sideShape=100%`: `drive≈8, hardMix=1` → maximum drive, hard clipping, fully normalised.

The side is the HP filter's output for `shaped − mono`, so only the harmonics the
shaper added appear in it. Bass below the cutoff is therefore the clean mono signal in
both channels.

## M/S matrix

The encode step sums to mono before the FIR stage (stereo difference is not extracted):

```
mono = (L + R) / 2          ← a silent L or R (mono track) is replaced by the other
mid  = mono delayed 64 samples
side = HP(shaped(mono) − mono)
L_out = mid + side
R_out = mid − side
```

At shape 0 the side is zero and a mono source comes out at unity gain in both channels.

## Parameters

### Controls

| Parameter   | Range      | Default | Notes                                                  |
|-------------|------------|---------|--------------------------------------------------------|
| Duck Depth  | 0–100 %    | 80 %    | 0 = no ducking, 100 = full silence on sidechain hit   |
| Attack      | 1–500 ms   | 10 ms   | Envelope follower rise time                            |
| Release     | 10–2000 ms | 100 ms  | Envelope follower fall time / pump recovery            |
| M/S Cutoff  | 50–5000 Hz | 200 Hz  | LP/HP crossover; typical: 80–300 Hz                   |
| Side Shape  | 0–100 %    | 0 %     | Distortion amount on side path; 0 = mono output, 100 = hard clip with drive 8× |
| Wet         | 0–100 %    | 100 %   | Wet/dry mix; dry is latency-compensated original stereo |

### Read-only meters

| Parameter      | Symbol         | Range | Notes                                          |
|----------------|----------------|-------|------------------------------------------------|
| Input Level    | `input_level`  | 0–1   | Peak of main input with slow ballistic decay   |
| Sidechain Level| `sc_level`     | 0–1   | Current envelope follower output               |
| Duck Gain      | `duck_gain`    | 0–1   | Current VCA gain (1 = no duck, 0 = full duck)  |
| Output Level   | `output_level` | 0–1   | Mean-abs output with fast-release ballistic    |

## Effective operating modes

| Mode | sideShape | Wet | Result |
|------|-----------|-----|--------|
| Ducker + mono EQ | 0 % | 100 % | Clean bass extraction, mono output above cutoff |
| Ducker + stereo synthesis | > 0 % | 100 % | Harmonics in side create width above cutoff |
| Dry blend | any | < 100 % | Mixes in latency-compensated original stereo |
| Pure EQ (no duck) | any | any | Set Duck Depth = 0 % to disable ducking |

## DPF mapping notes

- **NUM_INPUTS = 4**: inputs 0–1 are main stereo, inputs 2–3 are sidechain. Sidechain
  ports are placed in a custom port group (ID 100) named "Sidechain" via `initPortGroup`.
- **NUM_OUTPUTS = 2**: standard stereo output in the built-in stereo port group.
- **WANT_TIMEPOS = 0**: plugin is not transport-aware.
- **Latency**: 64 frames reported via `getLatencyInFrames()`; hosts should delay-compensate
  other tracks accordingly. The dry delay line is also 64 samples so wet and dry signals
  are time-aligned at any wet/dry ratio.

## Known limitations / future work

- The FIR coefficients are recomputed sample-accurately on cutoff change (per block).
  For very fast parameter automation this may introduce brief tonal artefacts; a
  cross-fade between old and new coefficients would eliminate them.
- The envelope follower uses peak detection on the louder sidechain channel; a true
  RMS option could be added as a future parameter.
- At `sideShape=0%` the wet output is mono. A stereo-preserving mode that keeps the
  original L−R difference in the HP path would be a straightforward routing alternative.
- Attack/Release slider nudge in the UI uses a linear step for a log-scale parameter;
  a finer log step would be more ergonomic.
