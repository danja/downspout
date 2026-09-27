# primefold — design notes

`primefold.vst3` — prime-harmonic feedback pitch shifter (stereo audio effect).

Dry input is pitch-shifted directly to the prime ratios 2x, 3x and 5x.
Every non-prime harmonic is supplied by recirculation through the same three
shifters: 4x is 2x fed back into 2x, 6x is 2x into 3x (or 3x into 2x),
8x is 2x three times, 9x is 3x twice, 10x is 2x/5x, 12x is 2x/2x/3x,
15x is 3x/5x, and so on up to the loop-gain floor. There are no dedicated
shifters for composite ratios.

This document is written before the code, as requested. It fixes the three
decisions the brief demands: what the pitch shifters are, what the feedback
network is, and how gains stay bounded — plus the latency rule that keeps
every cycle causal.

## 1. Pitch shifters: single-tap SOLA voices, ratios 2/3/5

Each prime voice is a time-domain single-tap pitch shifter with
correlation-matched jump-backs (a SOLA design), not an FFT phase vocoder and
not a waveshaper:

- One ring buffer per voice per channel (`kRingSize = 8192` samples).
  A single fractional read pointer shared across channels (so the stereo
  image never swims) chases the write pointer at exactly N samples per
  sample. Between jump-backs the output is one linearly-interpolated tap:
  instantaneous pitch is exactly N by construction.
- Whenever the voice delay runs short, the pointer jumps back by a nominal
  `jump_N = K - 64*(N-1)` samples (`K` = Grain, 512/1024/2048), so every
  voice shares the same nominal delay (~K/2 average) and the three prime
  voices stay mutually aligned. The exact landing is a normalized-correlation
  match over ±96 samples / 64-sample context, computed on the mid signal and
  applied to both channels, and the splice is hidden by a 64-sample Hann
  fade from a ghost continuation of the pre-jump stream.
- A per-voice RBJ low-pass at `fs/2N` sits before the memory write: playing
  memory back N times faster maps input content above `fs/2N` past Nyquist,
  so the 5x voice is necessarily darker than the 2x voice. Cost is ~5 MACs
  per sample per channel; coefficients are fixed at `activate`.
- No allocation after `activate`; no randomness anywhere (jump offsets come
  from the signal, so identical input gives identical output at any block
  size — asserted by the core tests); RT-safe; sample-rate independent.

Why not the obvious dual-tap sawtooth crossfade (two taps offset by half a
window, raised-cosine faded)? It was prototyped first and measured wrong: a
full-cycle crossfade drags the effective reference backward by W/2 per half
cycle, flattening 2x/3x/5x to ~1.85x/2.70x/4.41x on a 220 Hz sine, and the
error is content-dependent so it cannot be calibrated out. Narrowing the
fade to the wrap instants removes the drag but exposes the W-sample
discontinuity as HF hash (+1200 cents of spurious zero crossings). The SOLA
single tap has neither defect: carrier accuracy measures exact (440/660 Hz,
1109 Hz for 1100 Hz target — 14 cents, inside test tolerance) with only a few
percent residual fade hash.

Rejected alternatives, recorded so they stay rejected:

- FFT / phase vocoder: best quality on polyphonic material, but 4–8x the
  CPU, 2–4x the latency, needs scratch FFT buffers and careful RT
  discipline. Unjustified for a harmonic generator whose voices are mixed
  as overtones of the same input.
- Static nonlinearity (rectify / Chebyshev): `|x|` and `x^2` do make octave
  energy, but they are harmonic distorters, not pitch shifters: they do not
  shift a melody by 3x/5x and they intermodulate polyphonic input. The brief
  asks for pitch shifters, so this fails the contract.
- Async granular clouds: non-deterministic grain scheduling complicates the
  bounded-gain proof and the block-size-invariance test.

Voice latency varies sawtooth-fashion between ~8 and ~K+8+96 samples as the
pointer chases and jumps; the worst case (`K + 8 + 96`) is what the plugin
reports to the host. All three voices share the same nominal delay, so they
are mutually time-aligned to within the ±96-sample search window.

## 2. Feedback network: all-to-all recirculation through one explicit delay

Topology, per sample, per channel (same coefficients on L/R, shared phases
so the stereo image does not swim):

```
dry ----+----------------------------> dry delay (W) ----+
         |                                               v
         +--> voice inputs u_2, u_3, u_5 --> shifters --> y_2, y_3, y_5 --+--> mix
                      ^                       |                          |
                      |                       v                          v
                      +-- tanh <- damp LP <- explicit delay D (256) <-- voices
```

- Voice inputs: `u_i = dry + fb * (1/sqrt(3)) * sum_j lp(dly(y_j))`.
  The `1/sqrt(3)` fan-in keeps the 3x3 all-ones routing energy-neutral
  (same junction argument as magneto's manifold): unit gains recirculate at
  unity, never at 3x.
- `dly` is a dedicated 256-sample ring per voice per channel holding voice
  outputs. It exists only to make every cycle explicit (see §4).
- `lp` is a one-pole low-pass per voice per channel driven by **Damp**;
  it is the per-pass loop loss at high frequency and doubles as the
  brightness control.
- `tanh` on the summed feedback keeps large transients from pumping the
  loop; the loop itself stays linear for small signals so composites bloom
  rather than gate.

Composite map (paths through the network, shortest first):

| Harmonic | Shortest path        | Example chain              |
|----------|---------------------|----------------------------|
| 4x       | 2 -> 2              | in -> 2x -> D -> 2x -> out |
| 6x       | 2 -> 3 / 3 -> 2     | in -> 2x -> D -> 3x -> out |
| 8x       | 2 x3                | three passes through 2x    |
| 9x       | 3 -> 3              | two passes through 3x      |
| 10x      | 2 -> 5 / 5 -> 2     |                            |
| 12x      | 2 -> 2 -> 3         |                            |
| 15x      | 3 -> 5 / 5 -> 3     |                            |

Higher composites arrive later (one extra `K + D` per pass) and quieter
(loop gain < 1 per pass): the harmonic series builds up as a bloom, not as
an organ stop. That is intentional and documented on the panel.

## 3. How gains stay bounded (five layers, none optional)

1. **Passive voices.** Single-tap reads of clamped history (gain <= 1)
   plus a per-voice `fs/2N` low-pass that also bounds aliased foldback.
2. **Contractive routing.** Operator norm of the feedback matrix is
   `fb <= 0.90` (panel max; default 0.35). Fan-in `1/sqrt(3)`, LP gain <= 1,
   shifter gain <= 1, so worst-case small-signal round-trip gain is `0.90`.
   Each extra harmonic pass loses >= 10% before damping.
3. **Explicit loop loss.** `damp` LP (`a` from 1.0 down to ~0.05) sheds high
   frequency every pass; `tanh` bounds large-signal feedback without
   changing small-signal bloom.
4. **Output discipline.** Voice sum through `tanh`, dry/wet crossfade,
   mid/side width (gain-neutral at width 0/1), output level, DC blockers on
   both legs, hard clamp on every delay write, denormal flush on recursive
   states.
5. **Panic guard.** Per-block `isfinite` scan of the mix bus; on trip, clear
   all delay memory and LP states (same guard shape as magneto's
   `kNonFinitePanicCount`).

With `fb = 0.90` and all voice levels at 1.0 the network was measured (core
tests) to hold a full-scale sine burst without growth across 10 s; at the
default 0.35 it decays ~9 dB per recirculation pass.

## 4. Latency rules: explicit delay in every cycle, never compensated inside

- **Feedforward latency** `P = K + 8 + 96` (worst-case voice delay) is
  reported to the host with `Plugin::setLatency(...)` whenever Grain or the
  sample rate changes. The dry path carries a matching-`K/2` nominal delay
  (the voices' average) so dry/wet stays aligned on average; the residual
  sawtooth wander reads as a slow chorus rather than a fault.
- **Every feedback cycle carries the explicit delay `D = 256` samples**
  plus the voice's own minimum read delay (>= 8 samples). The loop is
  therefore causal by construction even at block boundaries: voice inputs
  for sample `n` are computed from feedback taps that contain only voice
  outputs up to sample `n - 1` at the newest.
- **Latency inside a cycle is never compensated.** No tap is advanced,
  predicted, or re-aligned to "undo" voice delay + `D`. Attempting to do so
  would be non-causal (it asks for a shifter output before its input exists)
  and would hide the bloom the network exists to produce. Composites are
  late by roughly `k * (K + D)` for `k` passes; the panel's ladder diagram
  shows this as depth rather than as a fault.
- Changing Grain calls `setLatency` and re-clears delay memory; the loop
  never interpolates across the size change.

## 5. Control rate versus audio rate

No control-rate decimation: all coefficients (`fb`, LP `a`, levels, mix,
width, dry-delay length) are smoothed per sample with a 10 ms one-pole
toward the panel value so automation never clicks. Phases advance per
sample. The only per-block work is the `isfinite` guard and the clip-lamp
decay.

## 6. Parameters (10 in + 2 status out)

dry, lvl2, lvl3, lvl5, feedback (0–0.90), damp, grain (512/1024/2048),
mix, width, level; outputs out_latency (samples) and out_clip (lamp).
CC: 1 = feedback, 2 = mix, 7 = level, written through to host params so
automation and panel agree. Reported latency is the worst-case voice delay
(grain + 8 + 96); dry carries the nominal average (grain/2).
