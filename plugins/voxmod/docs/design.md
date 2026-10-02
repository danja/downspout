# Voxmod design

## Signal flow

```
in 1/2 ─┬─────────────────────────────► carrier (analysed)
        │                                     │
        │                          ┌──────────┴──────────┐
        │                          │  filter bank, N bands│
        │                          │  envelope follower   │
        │                          └──────────┬──────────┘
        │                                     │ gains
in 3/4 ─┼────────────────────────────────────┴──► resynthesised modulator
        │                                                    │
        └──► × internal oscillator ──► ring mod ────────────┤
                                                             ▼
                                                        out 1/2
```

`Mix` crossfades the two engines. At 0% only the ring-modulated path is live;
at 100% only the vocoder is. The DSP keeps running either way, so `Bypass` is a
true bypass rather than a branch around the processing.

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
`testSampleRateIndependence` pins that.

The band gate skips any band whose envelope falls below `kBandGate`. Without it,
silence in the carrier would sum every band's noise floor into the output and
lift the modulator's noise.

## Status outputs

Three read-only parameters report what the processor actually measured, not what
the parameters say:

- `out_carrier_hz` — the effective carrier frequency
- `out_sibilance` — the share of carrier energy above the top bands. Above 0.7
  the carrier is mostly hiss and a vocoder cannot follow it, which is why the
  panel switches the meter to a warning colour.
- `out_reduction` — the modulator envelope level in dB, i.e. how hard the band
  gate is pulling the modulator down

## Bank rebuilds

The filter bank is rebuilt only when the band count, the sample rate or
`Formant` changes — `bankSerial` counts rebuilds so a test can prove an unchanged
bank is not rebuilt per block. Rebuilding resets no envelope state, so parameter
moves do not make the vocoder flinch.

## Known limitations

- `estimateCarrierHz` currently returns the parameter value rather than a
  true pitch estimate. A real estimate needs the carrier's spectral peak tracked
  across blocks; the ring modulator path (internal carrier) is exact, and the
  input path is approximate. The status output is honest about this by
  definition: it reports the oscillator frequency when there is one.
- `Ring Ratio` is relative to `Ring Freq` and to the modulator's pitch, not to a
  measured modulator fundamental. Quantising it musically makes it repeatable
  but does not lock it to the modulator.
