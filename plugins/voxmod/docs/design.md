# Voxmod design

## Signal flow

```
in 1/2 (modulator / source) ─┬─► × carrier (3/4 or osc) ──► ring mod ─┐
                             │                                       │
                             └─► filter bank ──► envelope gains ─────┤
                                      ▲                              │
in 3/4 (carrier / live IR) ───────────┤                              ├─► out 1/2
                             │        └─► ring carrier when routed   │
                             └─► live IR frames ──► FFT multiply ────┘
in 1/2 ─────────────────────────► source frames ──► FFT multiply
                                                    │
                                          IFFT + overlap-add
                                                    │
                                          feedback taps ──► 1/2, 3/4
```

`Mix` crossfades the ring mod and the vocoder. `Conv Mix` fades that pair
against the convolution engine, so the three engines form a three-way blend and
the legacy defaults are unchanged at `Conv Mix = 0`. Each engine has a `Level`
trim applied only at the mix point: turning one down cannot change what it feeds
into another, so `Sync` still takes the pre-level ring signal. The DSP keeps
running in every position; `Bypass` zeros all three engine amounts, so it mutes
the effect rather than passing the input through.

`Carrier` selects the ring modulator's multiplier as well as the vocoder's
analysis signal. At the default `input 3/4`, each source channel is multiplied by
the matching carrier channel whenever that bus is above
`kExternalCarrierGateDb`; the internal oscillator is the fallback when 3/4 is
silent, so one-input setups still work. At `internal osc`, Ring Freq / Ratio /
Shape apply as before. `Sync` still overrides the vocoder's analysis signal with
the ring-modulated result.

## Why the band edges are logarithmic

Band edges run from `kVocoderLowHz` (60 Hz) up through `Spread` octaves. A linear
bank would spend most of its bands above 2 kHz, where there are few formants to
resolve, and starve the 200-1200 Hz region that actually carries vowel identity.
Logarithmic spacing gives every band a comparable share of the spectrum, and the
test `testBandEdgesAreMonotonicAndLogarithmic` pins that: the ratio between
adjacent edges must be constant to within 5%.

Bands are RBJ bandpass with constant skirt gain rather than constant Q. A
constant-Q bank would make the resynthesis gain depend on how wide each band is,
which varies across the bank and would tilt the result.

## Why Formant moves the modulator side only

`Formant` transposes the *modulator* band's centre frequency while leaving the
*carrier* band's alone. Analysing the carrier at frequency *f* and resynthesising
the modulator at *f × 2^(shift/12)* relocates the envelope that shapes the
output — which is what a formant shift is. Moving both sides together would
just transpose the whole thing and change nothing audible about the vowel.

## Ratio quantisation

`Ring Ratio` spans 0.5 to 8, a four-octave range. Swept linearly, most of the
interesting sideband relationships sit in a small part of the track. It is
therefore quantised to 16 steps across −12 to +35 semitones, so a move of the
knob changes the sidebands in semitones and is repeatable. `quantiseRatio` is
tested to produce exactly `kRatioSteps` distinct values across the range.

## Live convolution cross-synthesis

The third engine treats both input pairs as live signals. Every 256 samples a
1024-sample Hann window is taken from inputs 1/2 (the source) and from inputs
3/4 (the "IR" feed); each is zero-padded to 2048, transformed, multiplied bin
by bin, inverse-transformed and overlap-added into the output with fourfold
overlap. The product is normalised by both the FFT and frame lengths: eight
frame-pair blocks overlap at a 256-sample hop, and the old half-window scale let
a pair of unit signals reach a gain of 512, which sounded like a clipper. A 6×
make-up gain brings ordinary tone pairs back to a useful level after that
correction. There is no stored impulse response: re-analysing the IR feed on
every hop is what makes the convolution move with the performance.

The 2048-point transform makes each frame pair a *linear* convolution of the
two windows instead of a circular one, so an impulse in the IR frame delays the
source without wrapping the frame tail into its head. `testConvolutionFramePairIsFrameMultiplication`
pins that with a delta IR, and the streaming tests check that a silent IR gives
no output and that changing the IR changes the result. Frame multiplication is
commutative, so this is cross-synthesis rather than a directional filter: the
contract that matters is that a signal must be present on *both* pairs.

At fourfold overlap the earliest contribution from a frame appears almost one
frame before its newest sample, so the dry ring/vocoder path is not time-aligned
with the wet convolution. The convolution engine is a colouration effect and
the two are faded rather than summed at equal level, so this is audible only
around intermediate `Conv Mix` settings.

`Src Feedback` and `IR Feedback` mix the output one frame back into the source
and IR frame buffers respectively. The one-frame tap avoids an algebraic loop
inside a frame, and both controls are capped at 95 %. The tap is driven up for
audibility and then soft-limited to ±1, the overlap-add history is bounded, and
the wet branch has an instant-attack peak limiter before it joins the mix. Those
three stages stop the loop accumulating without flattening the ring/vocoder
paths, which are deliberately hotter and untouched by the limiter. Defaults are
zero, which leaves the convolution strictly feed-forward.

## Band-limited oscillator

The saw and square shapes use polyBLEP correction, and triangle is built from an
integrated BLEP-corrected square. This matters more here than in a normal synth:
the oscillator's job is to produce *only* the carrier, and a naive discontinuity
would fold broadband aliases into the modulator's spectrum, drowning out the
discrete sidebands that make a ring modulator sound like one.

The triangle integrator is clamped rather than leaky — a leaky integrator would
drift over a long run and needs a DC blocker to hide it.

## Envelope followers

Both followers are asymmetric (fast attack, slow release) and time-based:
the coefficients are `exp(-1/(τ·fs))`, so the response is identical at 44.1 kHz
and 96 kHz and does not depend on the host's block size.
`testSampleRateIndependence` pins that. The ring oscillator's phase advance is
also divided by the host sample rate, so `Ring Freq × Ring Ratio` is the same
pitch at every rate; `testRingOscTracksHostSampleRate` pins that.

The band gate skips any band whose envelope falls below `kBandGate`. Without it,
silence in the carrier would sum every band's noise floor into the output and
lift the modulator's noise.

## Status outputs

Three read-only parameters expose live measurements and the generated oscillator:

- `out_carrier_hz` — the smoothed ring oscillator frequency actually generated by
  the core. It is the vocoder carrier only when `Carrier = internal osc` and
  `Sync` is off. With the default `Carrier = input 3/4` it is the ring mod's
  fallback oscillator, not a pitch estimate of the external carrier; the panel
  labels it `Ring osc` for that reason.
- `out_sibilance` — the share of carrier energy above the top bands. Above 0.7
  the carrier is mostly hiss and a vocoder cannot follow it, which is why the
  panel switches the meter to a warning colour.
- `out_reduction` — the modulator envelope level in dB, used as a proxy for band
  gate activity
- `out_mod_level` / `out_ir_level` — smoothed RMS of inputs 1/2 and 3/4 in dB.
  They bypass `Bypass` and the engine levels, so a silent `out_ir_level` directly
  shows the common host mistake of not routing the second stereo input bus.

## Bank rebuilds

The filter bank is rebuilt only when the band count, the sample rate or
`Formant` changes — `bankSerial` counts rebuilds so a test can prove an unchanged
bank is not rebuilt per block. Rebuilding resets no envelope state, so parameter
moves do not make the vocoder flinch.

## Known limitations

- The external carrier is not pitch-tracked. `estimateCarrierHz` is only a
  parameter-derived fallback used before the first audio block; after that the
  status output reports the generated ring oscillator. A true external-carrier
  estimate would need a spectral peak tracked across blocks.
- `Ring Ratio` is a multiplier on `Ring Freq`, not a ratio to a measured modulator
  fundamental. Quantising it musically makes it repeatable but does not lock it
  to the source on 1/2.
