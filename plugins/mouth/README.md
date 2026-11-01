# Mouth

Vowel filter. Five parallel formant band-pass filters morph between A, E, I, O and U on an x-y
pad, for bass, tenor, alto and soprano voices. It puts a vowel on any sound.

Magneto look: header, accent-banded panels, an x-y vowel pad laid out like a phonetics chart
(vertical = how open, horizontal = tongue front to back) with the live formant frequencies, a
plot of the filter response, and the voice controls.

## Signal flow

```
in L/R -> [drive] -> 5 parallel formant band-passes -> sum (+ air) -> output -> mix -> out L/R
                       (frequency, bandwidth, level from the vowel blend)
```

Left and right use their own coefficients so Spread can detune them against each other.

## Controls

| Control | Meaning |
|---|---|
| Vowel pad (X, Y) | Blend of A, E, I, O, U by inverse-distance weighting; exact on a vowel |
| Voice | Bass, Tenor, Alto, Soprano formant table |
| Shift | -12 to +12 st on every formant frequency |
| Resonance | Bandwidth: 50 % as tabulated, 0 % four times wider, 100 % four times narrower |
| Spread | Detunes formants 2-5 in opposite directions per side (stereo width) |
| Drive | 0-24 dB tanh saturation before the filters (off at 0) |
| Air | Unfiltered highs (above about 4 kHz) blended into the wet sound |
| Mix, Output | Dry/wet (0 % is bit-exact dry) and wet level |
| CC Channel | Drift CC 1-4 -> Vowel X, Vowel Y, Shift, Resonance (0 = off) |

## Notes

- No latency, no plugin state: every setting is a host parameter.
- Vowel data is the Csound formant table; see `docs/design.md` for the design and the open questions.
- Not auditioned or tried in a host yet.
