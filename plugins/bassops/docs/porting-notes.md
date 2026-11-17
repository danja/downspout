# bassops – design notes

## Overview

bassops is an original Downspout plugin (no LV2 source to port from). It combines two
common bass-focused processing techniques into a single 4-in/2-out effect:

1. **Sidechain ducker** – a kick-drum send on inputs 3/4 ducks the main signal on 1/2.
2. **M/S delay + stereo synthesis** – after ducking, the stereo input is split into
   mid = (L+R)/2 and difference = (L−R)/2 (a silent main L or R, i.e. a mono track, is
   treated as a copy of the other side first). Both are delayed by the FIR latency, so
   `mid ± diff` reconstructs the ducked stereo pair and the source image is carried
   through the wet path. A waveshaper distorts the mid; the harmonics it adds,
   high-passed above the cutoff, form an extra synthetic side. With side shape at zero
   that side is exactly zero and the ducked stereo input is reproduced unchanged.

## Signal flow

```
inputs 3-4 (sidechain)
    └─ peak envelope follower ─┐
                                ↓
inputs 1-2 (main) ──── VCA (duck gain)
                                ├─ mid=(L+R)/2 ─── 64-sample delay ──────────────────┐
                                ├─ diff=(L−R)/2 ─ 64-sample delay ──────────────────┤
                                └─ [waveshaper] − mid ── HP FIR ────────────────┐    │
                                                                                ↓    ↓
                                                                          decode mid±diff±side
                                                                                │
                             dry (64-sample delayed) ◄─── wet/dry mix ◄───────────┘
                                                                             outputs 1-2
```

Decode:
```
outL = wetScale × (midF + diffF + sideF) + dryScale × delayedInL
outR = wetScale × (midF − diffF − sideF) + dryScale × delayedInR
```

At `sideShape=0%`: `side` is zero, so `mid ± diff` reproduces the ducked stereo input
exactly (apart from the reported FIR latency), preserving the source image.
At `sideShape>0%`: side = HP(shaped − mid), the harmonics the waveshaper adds above the
cutoff; they appear in anti-phase in L and R, so they cancel when the output is summed
to mono and the bass is untouched.

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

The waveshaper runs on the mid signal before it enters the HP convolution buffer.
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

The side is the HP filter's output for `shaped − mid`, so only the harmonics the
shaper added appear in it. Bass below the cutoff is therefore the clean ducked stereo
pair, reconstructed from mid and difference.

## M/S matrix

The encode step extracts mid and difference before the FIR stage; both share the same
64-sample delay so they stay aligned:

```
L, R   = ducked input (a silent L or R, i.e. a mono track, is replaced by the other)
mid    = (L + R) / 2        delayed 64 samples
diff   = (L − R) / 2        delayed 64 samples
side   = HP(shaped(mid) − mid)
L_out  = mid + diff + side
R_out  = mid − diff − side
```

At shape 0 the side is zero and the ducked stereo input comes out at unity gain (a mono
source appears in both channels).

## Parameters

### Controls

| Parameter   | Range      | Default | Notes                                                  |
|-------------|------------|---------|--------------------------------------------------------|
| Duck Depth  | 0–100 %    | 80 %    | 0 = no ducking, 100 = full silence on sidechain hit   |
| Attack      | 0–500 ms   | 10 ms   | Envelope follower rise time; 0 = instant                |
| Release     | 0–2000 ms  | 100 ms  | Envelope follower fall time / pump recovery; 0 = instant |
| M/S Cutoff  | 50–5000 Hz | 200 Hz  | LP/HP crossover; typical: 80–300 Hz                   |
| Side Shape  | 0–100 %    | 0 %     | Extra distortion on the side path; 0 = ducked stereo unchanged, 100 = hard clip with drive 8× |
| Wet         | 0–100 %    | 100 %   | Wet/dry mix; dry is latency-compensated original stereo |
| Bass 90°    | Off/Left/Right | Off | Bass below the cutoff shifted 90° between channels (see below); UI: click the button in the Controls header |
| Delay (beats) | Off, 1/64 … 1 beat (12 steps) | Off | Main-input delay locked to the host tempo (see below); horizontal slider under the vertical ones |
| Delay (ms)  | 0–200 ms   | 0 ms    | Main-input delay in milliseconds, added to Delay (beats) |

**Ring mod:** with Attack and Release both at 0 the follower is bypassed and the main signal is
multiplied by the signed sidechain (larger-magnitude channel): `gain = (1 - d) + d * sc`, d = Duck Depth.
0 % is dry, 100 % is a pure ring mod (sum and difference tones). If either is above 0 it is the
ordinary ducker. The Duck Gain meter shows `|gain|` clamped to 1. In the UI the bottom of the
Attack/Release slider travel is a dead zone that reads 0.

### Bass 90° (not in the LV2 original)

The bass part after the filter is `LP(mono)` from the existing FIR (aligned with the mid, so no extra
latency). With Bass 90° on, that bass is swapped, in each channel, for one leg of a 12-pole
allpass Hilbert pair (Niemitalo coefficients, ±0.7° over ~10 Hz to 20 kHz, flat magnitude): the
chosen channel gets the leg that is 90° off the other. Output per channel is
`mid ± side + (leg − bass)`, so nothing above the cutoff changes. An IIR pair was used because a
129-tap FIR Hilbert cannot reach 90° at 50 Hz.

- Off (default): the network is never stepped and its state is cleared; cost is zero beyond the
  existing signal path. Switching on starts from clean state (a short settle, not crossfaded).
- Both channels' bass takes an allpass leg, so with Wet < 100 the bass is no longer phase-aligned
  with the (pure-delay) dry path and partly cancels/decorrelates against it.
- Serialized as `bassPhase` (absent in older projects = Off). DPF parameter appended last
  (`bass_phase`, index 19) so existing indices are unchanged.

### Input delay (not in the LV2 original)

A second way to get a bass out from under a kick: instead of (or as well as) lowering the bass,
hold it back so it starts just after the kick. Two sliders under the vertical ones delay the main
inputs 1–2; the sidechain is not delayed. They add:

- **Delay (beats)**: 12 steps, `0, 1/64, 1/32, 1/16, 1/12, 1/8, 1/6, 1/4, 1/3, 1/2, 3/4, 1` of a beat
  (`kBeatFractions`), converted with the host tempo every block (`bbt.beatsPerMinute`; DPF supplies
  120 when the host gives none, and a tempo that is zero, negative or not finite is read as 120).
  A tempo change moves the delay. `WANT_TIMEPOS` is therefore 1.
- **Delay (ms)**: 0–200 ms.
- The total is limited to `kMaxDelaySeconds` = 2 s (a whole beat below 30 bpm is cut to it).

Where it sits: straight after the mono-feed fix and the input meter, before the ducking VCA, the mono
sum and the dry path. So the ducker sees the delayed bass (the bass is ducked *and* late), and Wet 0 is
still the delayed input. It is a pure time shift, not an echo, and is **not** reported to the host as
latency (that would defeat it); only the FIR's 64 samples are.

Implementation (`InputDelay`): a per-channel ring buffer sized by `prepare(state, sampleRate)` to
`ceil(2 s × sr) + 2`. Called from the constructor and `sampleRateChanged()`; a host that never calls it
gets it on the first block, once. Settled, the output is a whole-sample read, exact and unfiltered. When
the target changes (slider, automation, tempo) it crossfades between the old and new tap over 10 ms;
a further change during a fade waits for the fade to finish, so there is never a jump or a pitch glide.
The first block after activation starts settled instead of fading in from zero. Activation clears the
buffer, never the setting. The resolved total is exposed as the read-only `status_delay_ms` for the
panel (the beat row shows its step in ms as `status − ms slider`).

Serialized as `delayBeat` and `delayMs` (absent in older projects = off). DPF parameters appended after
`bass_phase`: `delay_beat` (index 20), `delay_ms` (21), `status_delay_ms` (22).

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
| Ducker (stereo preserved) | 0 % | 100 % | Ducking only; the source stereo image is carried through unchanged |
| Ducker + stereo synthesis | > 0 % | 100 % | Added harmonics in side create extra width above cutoff |
| Dry blend | any | < 100 % | Mixes in latency-compensated original stereo |
| Pure EQ (no duck) | any | any | Set Duck Depth = 0 % to disable ducking |

## DPF mapping notes

- **NUM_INPUTS = 4**: inputs 0–1 are main stereo, inputs 2–3 are sidechain. Sidechain
  ports are placed in a custom port group (ID 100) named "Sidechain" via `initPortGroup`.
- **NUM_OUTPUTS = 2**: standard stereo output in the built-in stereo port group.
- **WANT_TIMEPOS = 1**: only for the tempo that locks Delay (beats); playing/stopped and position are not used.
- **Latency**: 64 frames reported via `getLatencyInFrames()`; hosts should delay-compensate
  other tracks accordingly. The dry delay line is also 64 samples so wet and dry signals
  are time-aligned at any wet/dry ratio.

## Known limitations / future work

- The FIR coefficients are recomputed sample-accurately on cutoff change (per block).
  For very fast parameter automation this may introduce brief tonal artefacts; a
  cross-fade between old and new coefficients would eliminate them.
- The envelope follower uses peak detection on the louder sidechain channel; a true
  RMS option could be added as a future parameter.
- (Resolved) The wet path used to collapse to mono at `sideShape=0%`; it now delays and
  reconstructs the mid and difference separately, so the ducked stereo image is kept.
- Attack/Release slider nudge in the UI uses a linear step for a log-scale parameter;
  a finer log step would be more ergonomic.
- Delay (beats) follows the tempo with a 10 ms crossfade, so a very fast tempo ramp retimes in steps
  rather than continuously; the ms slider shifts by whole samples (21 µs at 48 kHz).
- Bass 90°, ring mod and the input delay are additions to the LV2 original. Bass 90° is not crossfaded when
  switched; ring mod switches abruptly when attack/release reach 0.
