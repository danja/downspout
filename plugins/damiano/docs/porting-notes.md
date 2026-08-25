# Damiano — Design notes

**Status:** initial implementation  
**Type:** original effect (not an LV2 port)

## Purpose

Stereo distortion plugin with six saturation/waveshaping modes and MIDI CC control
of the drive parameter. Intended for use with the Drift MIDI CC modulator, which
routes configurable CC lanes to the Damiano drive control in real-time.

## Signal path

```
stereo in → [tone shelf] → [distortion mode] → [dry/wet mix] → [output gain] → stereo out
```

MIDI CC messages arrive on the configured channel and CC number. A matching CC
overrides the drive parameter for that and subsequent blocks until CC is disabled.

## Distortion modes

| # | Name      | Algorithm                                              |
|---|-----------|--------------------------------------------------------|
| 0 | Soft      | Cubic polynomial: `x*(1.5 - 0.5*x²)`, hard clip ±1   |
| 1 | Tanh      | `tanh(drive*x) / tanh(drive)`, normalized ±1          |
| 2 | Fuzz      | Asymptotic: `sign(x)*(1 - exp(-drive*4.4*|x|))`       |
| 3 | Overdrive | Tanh with small DC bias, normalized; Tube-Screamer style|
| 4 | Tube      | Asymmetric: tanh positive half, `x/√(1+x²)` negative  |
| 5 | Wavefold  | Triangle wavefolder applied `foldCount` times          |

## Tone control

One-pole high-shelf per channel. Crossover at ~3 kHz.  
Implementation: `output = input + toneAmount * (input - onepole_LP(input))`  
`toneAmount = (tone - 50) / 50 * 0.8`  
50 = flat, 0 = -0.8 × high band removed, 100 = +0.8 × high band boosted.

## Wavefold detail

Triangle wavefolder using `fmod`-based closed-form (O(1), RT-safe):
```
u = drive * input + 1
t = fmod(u, 4); if t < 0: t += 4
output = (t < 2) ? t - 1 : 3 - t
```
Applied `foldCount` (1–8) times. Each successive fold takes the previous output
(bounded ±1) and folds it again with the same drive — increasing harmonic richness.

## MIDI CC routing

Drift outputs CC messages on configurable CC numbers (default: CC 1–4 for lanes 1–4).
Set `cc_drive` to match the Drift lane CC number and `cc_channel` to the Drift channel.
When a matching CC arrives, `effectiveDrive = 1.0 + (cc_value / 127) * 9.0`.

Setting `cc_drive = 0` disables CC control; the drive parameter is used directly.

## Asymmetric tube mode rationale

Positive half uses `tanh(x*drive)/tanh(drive)` (symmetric tanh, odd harmonics).  
Negative half uses `x/√(1+x²)` (algebraic sigmoid, different spectral character).  
The mismatch between half-cycles introduces even harmonics, mimicking class A
amplifier topology where the operating point is asymmetric around the bias point.
