---
title: Chipper
order: 38
bundle: chipper.vst3
kind: Audio effect
role: Lo-fi bit crusher
screenshot: /assets/plugins/chipper.png
summary: 1980s video game lo-fi processor. Bitwise quantization (bit depth 1–16), sample-rate reduction via sample-and-hold (divisor 1–64), and randomised clock jitter for vintage digital harshness.
---

## Opinion

The sample-rate reduction is the most characterful control — dropping to sr/8 gives instant Game Boy, sr/16 pushes into Atari territory. Bit depth works best below 6 bits where the quantisation noise becomes musically aggressive rather than merely grainy. Jitter is subtle at low settings but adds organic timing instability that separates Chipper from clinical bit-crushers.

## Functionality

Chipper implements two classic lo-fi algorithms in series: a configurable sample-and-hold (sample-rate reduction with optional timing jitter) followed by uniform midtread quantisation (bit depth reduction). The order matches hardware sample-rate reduction circuits, which hold the analogue input before digitising.

### Signal chain

```
Input → Sample-and-Hold (Rate Div, Jitter) → Bit Quantise (Bit Depth) → Dry/Wet Mix → Output Gain → Out
```

### Parameters

| Parameter   | Range      | Default | Notes                                                                   |
|-------------|------------|---------|-------------------------------------------------------------------------|
| Bit Depth   | 1–16 bits  | 8       | Quantisation resolution; 16 = no reduction; 8 ≈ original Game Boy DAC  |
| Rate Div    | 1–64       | 8       | Hold every Nth input sample; 1 = no reduction; sr/8 ≈ 6 kHz at 48 kHz |
| Jitter      | 0–100 %    | 0 %     | Fraction of Rate Div randomised per hold cycle; 0 = deterministic       |
| Mix         | 0–100 %    | 100 %   | Dry/wet blend; 0 % passes input unchanged                               |
| Output Gain | −12 to +12 dB | 0 dB | Post-processing level trim                                             |

### DSP notes

The sample-and-hold uses a per-channel held value updated every Rate Div frames. When Jitter > 0, a xorshift32 PRNG randomises the hold length by ±floor(jitter × rateDiv / 2) frames each cycle, simulating the clock instability of cheap 1980s DAC circuits. Bit quantisation uses midtread uniform rounding: `round(x × 2^(N−1)) / 2^(N−1)`.

### Status

Core DSP and tests are complete. Screenshot capture pending.
