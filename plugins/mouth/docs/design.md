# Mouth: design

Status: built (v0.1.0). Core, VST3 target, tests, screenshot and docs lists are done; not auditioned
and not run in a host. What differs from the original proposal: the Tilt control is called **Air**
(parameter `tilt`), **Output** was added, the pad uses inverse-distance weighting around five
vowel points, and the wet level is normalised so white noise keeps about its level (measured
within -2.3 to +0.4 dB across voices and vowels), which settles the first open question. The
vowel sequencer and the "3 formants plus a shelf" variant were not built. The proposal below is
kept as written.

## Purpose

Impose a vowel on any audio source (synth, drums, noise, guitar) with a parallel bank of
band-pass formant filters, morphed between vowels from an x-y pad. Gap in the current
set: `quefrency` only moves formants a voice already has, `voxmod` needs a second
(modulator) input, `octet` is a general EQ. Nothing here gives a "talking filter".

## Signal flow

```
in L/R --> [pre drive] --> 5 parallel band-pass formants (shared by both channels' coefficients)
                              |  F1..F5: frequency, bandwidth, gain
                              v
                           sum --> [tilt/air blend of dry high band] --> [mix] --> out L/R
```

- Formants are state-variable (TPT/ZDF) band-pass filters: stable under fast modulation,
  which a morphing filter needs (the same reason `ladder` uses ZDF).
- Frequency and bandwidth interpolate in log domain between vowel targets, gain in dB.
  Smooth all targets (about 20 ms) so a pad drag or CC sweep does not zipper.
- Stereo: same coefficients on both channels; optional **Stereo spread** offsets F2..F5
  by up to +/- a few percent in opposite directions for width.

## Vowel data

Base table: five vowels A E I O U, per voice type (bass, tenor, alto, soprano), five formants
each: frequency (Hz), level (dB), bandwidth (Hz). The widely circulated Csound formant tables
are the starting point (soprano /a/: 800, 1150, 2900, 3900, 4950 Hz; 0, -6, -32, -20,
-50 dB; 80, 90, 120, 130, 140 Hz). **These numbers were written from memory of that table and
must be checked against a source (Csound manual appendix, or Peterson and Barney 1952
averages) before shipping**; hold an independent expected copy in the test, as the repo's
table-driven-data rule requires.

Optional later: a second row of vowels (AE, schwa, ER) for a 2-D map with more than five
corners.

## Controls (Magneto look and feel)

| Control | Kind | Notes |
|---|---|---|
| Vowel X / Vowel Y | x-y pad | Pad plots the vowels on the F1/F2 plane (F1 vertical inverted, F2 horizontal inverted, as in phonetics charts); position is a bilinear blend of the nearest corners |
| Voice | stepper | Bass / Tenor / Alto / Soprano |
| Shift | slider, -12..+12 st | Scales all formant frequencies (gender / size) |
| Resonance | slider | Scales bandwidths; narrow = vocal, very narrow = pitched ringing |
| Spread | slider | Stereo offset of upper formants |
| Drive | slider | Pre-filter saturation, keeps the sound alive on weak input |
| Tilt / Air | slider | How much unfiltered highs bleed through (breathiness, sibilants) |
| Mix | slider | Dry / wet |
| CC Channel | stepper | Drift convention: CC 1-4 -> X, Y, Shift, Resonance (see `plugins-summary.md`) |

Live display: the combined magnitude response of the five filters on a log-frequency plot
(same plot frame as Prism and Octet), plus the pad cursor.

## Modulation / animation

- X, Y, Shift, Resonance are ordinary automatable parameters, so Drift and host automation
  drive them.
- Phase 2 option: a built-in vowel sequencer (step list of vowel corners, tempo-synced),
  reusing the tempo plumbing from Prism. Not in v1.

## Tests (core, deterministic)

1. Parameter table: ids, ranges, defaults, outputs flagged.
2. Each vowel corner at neutral Shift/Resonance: measured peak frequencies of the magnitude
   response land within a tolerance of the table's F1/F2, using a Goertzel/sine sweep rather
   than reading the coefficients.
3. Morph is continuous: sweep X and Y in small steps, assert peak frequencies move
   monotonically and with no jump above a bound (documented worst case, swept over the whole
   range as the repo requires).
4. Shift of +12 st doubles every formant frequency (within tolerance); -12 halves it.
5. Resonance scales bandwidth: -3 dB width at minimum vs maximum, measured.
6. Mix = 0 is bit-exact dry; Mix = 1 with Tilt = 0 has no energy far above F5.
7. Stability: impulse response decays for every vowel x voice x extreme Shift/Resonance.
8. Fast modulation: alternate X between 0 and 1 every block; output stays finite and bounded
   (no zipper above the smoothing bound).
9. Block-size invariance and reset, as in Prism.
10. Every control has an audible effect (difference vs default on a noise input).
11. Speed: timing test, rerun alone before treating failure as a regression.

## Open questions

- Output level compensation: vowel gains are relative; decide whether to normalise so the
  loudest formant peak is 0 dB or the RMS on pink noise is unity (measure both).
- Whether 5 formants is worth the CPU against 3 plus a fixed high shelf; measure with the
  speed test before deciding.
- Low-pitched sources have no energy at F2-F5: Drive and the Tilt blend are the planned
  mitigation; confirm by listening.

## Wiring when it is built

Follow the `new-plugin` skill: plugin CMake, root option, `install.sh`,
`scripts/package-release.sh`, `.github/workflows/release.yml`, the docs list in `CLAUDE.md`,
`profile.ttl`, screenshot entry, `docs/pages/_products/mouth.md`, `plugins-summary.md` row
(and the CC line there if the Drift convention is adopted).
