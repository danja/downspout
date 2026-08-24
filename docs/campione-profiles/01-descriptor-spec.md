# Audio Sample Descriptor Specification

**Version:** 0.1.0-draft
**Status:** normative. Implementations claiming conformance MUST follow this document exactly.
**Extractor version string:** `sampleprofile-extractor/0.1.0`

Requirement keywords (MUST, SHOULD, MAY) are used in the RFC 2119 sense.

---

## 1. Scope and conventions

This document defines a fixed set of numeric descriptors computed from a single audio
sample, together with the analysis parameters, edge-case behaviour and canonical
ordering required to make those descriptors reproducible.

It does **not** define classification, instrument taxonomy, or MIDI mapping. See
`00-overview.md`.

### 1.1 Numeric conventions

- All intermediate accumulation MUST use `double` (IEEE 754 binary64), including sums
  over FFT bins and over frames. Single-precision accumulation produces
  platform-dependent results and is non-conforming.
- All emitted values are `xsd:double`.
- Serialised values MUST be rounded to 6 significant figures. This is what makes
  byte-identical output across platforms achievable.
- Frequencies are in hertz, times in seconds, levels in decibels.
- Decibel values are `20·log10(amplitude_ratio)` unless stated otherwise, floored at
  −120 dB. `log10(0)` MUST NOT be emitted.
- A descriptor that is undefined for the given input (see §6) MUST be **omitted** from
  the RDF output. It MUST NOT be emitted as `0`, `NaN`, or `-inf`.

### 1.2 Determinism requirement

For a given extractor version, identical input audio data MUST produce a
byte-identical Turtle document on every platform. Not merely identical descriptor
values — identical bytes. Conformance tests MUST include the synthetic reference corpus
described in §7.

This is achievable because of three decisions taken together: fixed 6-significant-figure
formatting (§1.1), skolemised IRIs in place of blank nodes (§1.3), and a fixed
statement order (§1.4). Implementations MUST NOT weaken any of the three.

### 1.3 IRI minting

Profiles MUST NOT use blank nodes. Every node in a profile document is given an IRI
derived from the sample IRI by fragment:

| Node | IRI |
|---|---|
| Sample | `urn:sha256:<contentHash>` |
| Profile | `<sample IRI>#profile-<extractorVersion>` |
| Extraction activity | `<sample IRI>#extraction-<extractorVersion>` |
| Frame statistic for descriptor *d* | `<sample IRI>#stat-<d>` |
| Band energy, band *n* | `<sample IRI>#band-<n>` |
| Envelope segment *n* | `<sample IRI>#env-<n>` |
| Classification by agent *a* | `<sample IRI>#class-<a>` |

Fragment components are lowercase, with `.` in version strings replaced by `-`.

Rationale, since implementers will be tempted to revert this: blank nodes make the
output non-diffable in version control, create identity problems when a profile is
re-ingested into a triplestore (reload produces fresh blank nodes and therefore
duplicate rather than replaced data), and make SPARQL over the corpus needlessly
awkward. Skolemising costs nothing and removes all three problems.

### 1.4 Serialisation

Reading and writing SHOULD use serd (ISC, no dependencies, plugin-suitable). Whatever
library is used, two constraints are normative:

1. **Numeric formatting is the extractor's responsibility, not the library's.** Doubles
   MUST be formatted by the extractor per §1.1 and passed to the serialiser as
   pre-formatted literal strings with an explicit `^^xsd:double` datatype. Passing a
   numeric value to a library and accepting its default rendering is non-conforming,
   because that rendering is neither specified here nor stable across library versions.
2. **Statement order is fixed** and follows the order of this document: container
   metadata (§3), then §4.1 through §4.10 in table order, then classifications ordered
   by `sp:rank`. Indexed nodes are emitted in ascending index order.

---

## 2. Preprocessing

Applied in this order.

### 2.1 Decode

Read PCM samples to `float32` in the range [−1, +1]. Integer formats are scaled by
`1 / 2^(bits−1)`. Float formats are used as-is and MUST NOT be clamped (recording
`sp:peakDbfs > 0` is informative).

Record from the container: `sampleRate`, `channelCount`, `bitDepth`, `frameCount`,
`encoding`.

### 2.2 DC removal

Subtract the arithmetic mean of the whole signal, per channel, before any other
processing. A DC offset otherwise corrupts ZCR, spectral centroid and RMS.

### 2.3 Mono downmix

`m[i] = (1/C) · Σ_c x_c[i]` over all `C` channels.

All descriptors except those in §4.8 (stereo) are computed on `m`. Note that
mono-summing an out-of-phase stereo sample can produce near-silence; when
`interChannelCorrelation < −0.5`, implementations MUST set `sp:phaseWarning true` and
SHOULD compute spectral descriptors on the louder channel instead of `m`, recording
`sp:downmixMode "left"` or `"right"`.

### 2.4 Peak normalisation

Let `P = max|m[i]|`. Analysis is performed on `m/P`. Absolute level is preserved
separately as `peakDbfs` (§4.2). If `P = 0` the sample is silent: emit only §3 container
metadata plus `sp:silent true`, and stop.

### 2.5 Amplitude envelope

The envelope `e[j]` is the RMS of `m/P` over a sliding rectangular window of **5 ms**
with a hop of **1 ms**, both rounded to the nearest whole sample, with the window
centred on the hop position and zero-padded at the boundaries. Envelope values are
therefore sampled at 1000 Hz regardless of the audio sample rate.

### 2.6 Time anchors

| Anchor | Definition |
|---|---|
| `t0` (onset) | time of the first envelope sample where `20·log10(e[j]) > −60` dB |
| `tPeak` | time of the maximum of `e[j]` at or after `t0` |
| `tEnd` | time of the **last** envelope sample at or after `tPeak` where `20·log10(e[j]) > −40` dB |

All temporal descriptors are measured relative to `t0`. The **effective region** is
`[t0, tEnd]`; all frame-based analysis is confined to it.

Rationale for last-crossing rather than first: samples with a re-excitation or a
reverb tail would otherwise be truncated at a momentary dip.

---

## 3. Container metadata

Emitted verbatim, not derived. See ontology terms `sp:sampleRate`, `sp:channelCount`,
`sp:bitDepth`, `sp:frameCount`, `sp:encoding`, `sp:contentHash`.

`sp:contentHash` is the lowercase hex SHA-256 of the decoded PCM payload **only** —
excluding all container headers and metadata chunks — computed on the raw bytes as
stored, before any preprocessing. This gives an identity that survives renaming,
re-tagging and container conversion.

---

## 4. Descriptors

### 4.1 Temporal (8 values)

| Term | Definition |
|---|---|
| `sp:durationSeconds` | `frameCount / sampleRate` — the whole file |
| `sp:effectiveDurationSeconds` | `tEnd − t0` |
| `sp:attackTimeSeconds` | `tPeak − t0` |
| `sp:logAttackTime` | `log10(max(attackTimeSeconds, 1e-4))` |
| `sp:temporalCentroidNormalised` | `(Σ_j t_j·e[j] / Σ_j e[j] − t0) / effectiveDuration`, over `j` in the effective region. Dimensionless, in [0,1] |
| `sp:crestFactorDb` | `20·log10(peak / rms)` over the effective region, on `m/P` |
| `sp:decaySlopeDbPerSecond` | slope of a least-squares line fit of `20·log10(e[j])` against `t_j`, over `j` from `tPeak` to `tEnd` |
| `sp:decayFitR2` | coefficient of determination of that fit, in [0,1] |

`decayFitR2` near 1 indicates single-exponential decay (a damped drum); lower values
indicate multi-modal or beating decay (cymbals, ringing metal). It is a useful
discriminator in its own right, not merely a quality metric.

### 4.2 Levels (2 values)

| Term | Definition |
|---|---|
| `sp:peakDbfs` | `20·log10(P)` — the pre-normalisation peak |
| `sp:rmsDbfs` | RMS of `m` (un-normalised) over the effective region, in dBFS |

### 4.3 Short-time Fourier analysis

| Parameter | Value |
|---|---|
| Window | Hann, periodic (`w[n] = 0.5·(1 − cos(2πn/N))`, n = 0..N−1) |
| Size `N` | 1024 samples |
| Hop | 256 samples |
| Scaling | magnitude spectrum `|X[k]|`, no normalisation |
| Bin frequency | `f_k = k · sampleRate / N`, k = 0 .. N/2 |

Window size is a deliberate compromise: 1024 at 44.1 kHz gives 23 ms time resolution,
adequate for a percussive attack, at 43 Hz frequency resolution, which is coarse for a
kick fundamental but perfectly adequate for the broadband shape descriptors below.
Fundamental estimation uses a separate long-window analysis (§4.6).

**Frame selection.** A frame is included if its window centre lies within `[t0, tEnd]`
**and** its total magnitude exceeds −60 dB relative to the loudest frame. Excluding
near-silent frames prevents the tail from dominating the summary statistics. If fewer
than 3 frames qualify, §4.4 and §4.5 descriptors are undefined (§6).

Bin 0 (DC) and bin N/2 (Nyquist) are excluded from all spectral sums.

### 4.4 Per-frame spectral descriptors

Computed for each included frame, then summarised (§4.5). Let `a_k = |X[k]|`,
`k = 1 .. N/2−1`, and `A = Σ a_k`.

| Descriptor | Formula |
|---|---|
| `spectralCentroid` | `Σ f_k·a_k / A` |
| `spectralSpread` | `sqrt( Σ (f_k − centroid)²·a_k / A )` |
| `spectralSkewness` | `Σ (f_k − centroid)³·a_k / (A · spread³)` |
| `spectralKurtosis` | `Σ (f_k − centroid)⁴·a_k / (A · spread⁴)` — **raw**, not excess; 3.0 for a Gaussian distribution |
| `spectralRolloff85` | lowest `f_k` where the cumulative sum of `a` from k=1 reaches 0.85·A |
| `spectralRolloff95` | as above at 0.95·A |
| `spectralFlatness` | `exp(mean(ln(p_k + ε))) / mean(p_k)` where `p_k = a_k²` and `ε = 1e-20`, evaluated over bins whose `f_k` lies in [50 Hz, Nyquist] |
| `spectralFlux` | `sqrt( Σ_k max(0, â_k[n] − â_k[n−1])² )` where `â` is the frame magnitude vector L2-normalised to unit length. Half-wave rectified. Undefined for the first frame, which is excluded from the flux summary |
| `zeroCrossingRate` | computed in the **time domain** over the same window span, in crossings per second: `(crossings / N) · sampleRate` |

Notes for implementers:

- Spread MUST be guarded: if `spread < 1e-9`, skewness and kurtosis are undefined for
  that frame and it is excluded from those two summaries only.
- Flatness is bounded [0, 1]. Restricting to ≥ 50 Hz keeps empty sub-bass bins from
  dragging the geometric mean to zero. This band restriction is normative.
- ZCR counts sign changes; samples exactly equal to zero do not count as a crossing and
  do not break a run.

### 4.5 Frame summary statistics (18 values)

For each of the nine descriptors in §4.4, emit `sp:mean` and `sp:stdDev` over the
included frames. Standard deviation is the **population** standard deviation
(divide by *n*, not *n*−1).

Emitted as `sp:FrameStatistic` nodes — see the ontology.

### 4.6 Attack, tail and dominant partial (7 values)

**Attack spectrum.** A single Hann-windowed 1024-point FFT beginning at `t0`.
**Tail spectrum.** The mean magnitude spectrum of all included frames whose centre lies
in the later half of the effective region, `[t0 + 0.5·effectiveDuration, tEnd]`.

| Term | Definition |
|---|---|
| `sp:attackCentroid` | spectral centroid of the attack spectrum |
| `sp:tailCentroid` | spectral centroid of the tail spectrum |
| `sp:attackTailCentroidDelta` | `attackCentroid − tailCentroid` |
| `sp:attackFlatness` | spectral flatness of the attack spectrum |
| `sp:tailFlatness` | spectral flatness of the tail spectrum |

**Dominant partial.** Computed from a separate, longer analysis: a Hann window starting
at `tPeak + 10 ms`, of length `min(4096, samples remaining to tEnd)`, zero-padded to
16384 before transforming. This gives roughly 2.7 Hz resolution at 44.1 kHz.

| Term | Definition |
|---|---|
| `sp:dominantPartialFrequency` | frequency of the largest magnitude peak above 30 Hz, refined by parabolic interpolation over the peak bin and its two neighbours (on log-magnitude) |
| `sp:dominantPartialSalience` | peak magnitude divided by the mean magnitude of the whole analysed spectrum, then mapped to [0,1] as `1 − 1/(1 + ratio/8)` |

A frequency-swept source (a kick drum, notably) will smear across this long window and
report a low salience. This is correct behaviour and is itself diagnostic — do not
"fix" it by shortening the window.

### 4.7 Band energies (8 values)

Computed from the mean power spectrum over all included frames. Band edges, in hertz:

```
20, 60, 120, 250, 500, 1000, 2500, 6000, 20000
```

giving eight bands, indexed 0–7. Each band's value is its share of the total energy
across all eight bands, so the eight values sum to 1.0.

- Bins are assigned to the band containing `f_k`, with the lower edge inclusive and the
  upper edge exclusive.
- Energy below 20 Hz and above 20 kHz is discarded before normalisation.
- A band lying entirely above Nyquist is emitted with value `0.0` — not omitted. The
  vector must stay eight long.

Band energy shares are preferred to "primary FFT components" for classification: peak
picking is unstable between two takes of the same instrument, whereas the band
distribution is not.

### 4.8 Envelope segments (20 values)

The effective region `[t0, tEnd]` is divided into **10 equal segments**, indexed 0–9.

| Term per segment | Definition |
|---|---|
| `sp:rmsDb` | RMS of `m/P` over the segment, in dB relative to the maximum segment RMS, floored at −60 dB. Segment 0 is therefore usually, but not necessarily, 0.0 |
| `sp:highBandRmsDb` | the same measure computed on the signal high-passed at 2 kHz (2nd-order Butterworth, applied forward only), independently normalised to its own maximum segment |

Two decisions are normative and deliberate:

1. Segments span the **effective** duration, not the file duration. Trailing silence
   would otherwise compress the useful shape into the first segments and make padded and
   trimmed copies of the same sample look different.
2. Values are in dB relative to peak, not linear amplitude. This makes a closed hat and
   a crash cymbal shape-comparable, with absolute scale carried separately by
   `effectiveDurationSeconds` and `peakDbfs`.

The high-band envelope captures the common percussion signature of a noise burst
decaying faster than the body of the instrument.

### 4.9 Stereo (2 values)

| Term | Definition |
|---|---|
| `sp:interChannelCorrelation` | Pearson correlation of L and R over the effective region, in [−1, 1] |
| `sp:midSideRatioDb` | `20·log10(rms(mid) / rms(side))` where `mid = (L+R)/2`, `side = (L−R)/2`; clamped to [−96, +96] |

For mono input, emit `interChannelCorrelation = 1.0` and `midSideRatioDb = 96.0`.
For more than two channels, use channels 0 and 1 and set `sp:channelCount` accordingly.

### 4.10 Advisory flags

Cheap booleans that stop downstream consumers drawing nonsense from a profile of
something that is not a one-shot. All are emitted always.

| Term | Definition |
|---|---|
| `sp:silent` | `true` if `P = 0` (see §2.4) |
| `sp:clipped` | `true` if ≥ 8 consecutive samples are within 1 LSB of full scale, or `peakDbfs > 0` |
| `sp:multipleOnsets` | `true` if, after `tPeak`, the envelope rises by more than 12 dB from a local minimum that was itself ≥ 12 dB below the running peak |
| `sp:sustained` | `true` if `temporalCentroidNormalised > 0.45` and `decayFitR2 < 0.5` |
| `sp:phaseWarning` | `true` if `interChannelCorrelation < −0.5` (see §2.3) |

---

## 5. Canonical feature vector

For machine-learning consumers, the descriptors flatten to a **65-element** vector of
doubles in exactly this order. This ordering is normative and MUST NOT change within a
major version.

| Index | Contents |
|---|---|
| 0–7 | §4.1 temporal, in table order |
| 8–9 | §4.2 levels, in table order |
| 10–27 | §4.5 frame summaries: for each of the nine §4.4 descriptors in table order, mean then stdDev |
| 28–32 | §4.6 attack/tail: attackCentroid, tailCentroid, attackTailCentroidDelta, attackFlatness, tailFlatness |
| 33–34 | §4.6 dominant partial: frequency, salience |
| 35–42 | §4.7 band energies, index 0–7 |
| 43–52 | §4.8 `rmsDb`, segment 0–9 |
| 53–62 | §4.8 `highBandRmsDb`, segment 0–9 |
| 63–64 | §4.9 interChannelCorrelation, midSideRatioDb |

Undefined elements (§6) are `NaN` in the vector — in contrast to the RDF, where the
triple is omitted entirely. A parallel 65-bit validity mask SHOULD accompany the vector.

**Vector values are unscaled.** Feature scaling is the classifier's business, not the
extractor's, because the appropriate scaling depends on the training distribution and
must not be baked into a format that outlives any one model.

---

## 6. Undefined descriptors

| Condition | Undefined |
|---|---|
| Silent sample (`P = 0`) | everything except §3 metadata and `sp:silent` |
| Fewer than 3 included STFT frames | all of §4.4–§4.7 |
| Fewer than 2 frames after `tPeak` | `decaySlopeDbPerSecond`, `decayFitR2` |
| No tail frames (effective duration shorter than ~12 ms) | `tailCentroid`, `tailFlatness`, `attackTailCentroidDelta` |
| `spread < 1e-9` in a frame | that frame's skewness and kurtosis only |
| No spectral peak above 30 Hz | `dominantPartialFrequency`, `dominantPartialSalience` |
| Effective duration < 10 ms | all §4.8 envelope segments |

Very short samples (a 5 ms click) are legitimate input. They yield a sparse profile, and
that sparsity is information: a classifier can and should use it.

---

## 7. Conformance testing

An implementation is conformant when it passes all of:

1. **Analytic reference signals**, asserted to 1e-9 against values derived by hand:
   - 1 s 1 kHz sine, full scale — centroid ≈ 1000, flatness ≈ 0, ZCR ≈ 2000, kurtosis high
   - 1 s white noise, fixed seed — flatness ≈ 1, centroid ≈ Nyquist/2
   - exponentially decaying 100 Hz sine, τ = 0.1 s — `decaySlopeDbPerSecond` ≈ −86.9, `decayFitR2` ≈ 1.0
   - 10 ms click — most descriptors undefined per §6
   - pure silence, pure DC, single-sample impulse
   - full-scale square wave — `clipped` true
2. **Invariance**: peak-normalising, format-converting (16-bit ↔ 32-bit float), or
   appending trailing silence to an input MUST NOT change any descriptor except
   `peakDbfs`, `rmsDbfs`, `durationSeconds` and `bitDepth`.
3. **Cross-platform identity**: identical 6-significant-figure output on x86-64 and
   aarch64, Linux and macOS.
4. **Serialisation round-trip**: emitted Turtle parses under `serdi` and
   `riot --validate`, satisfies the SHACL shapes, and re-serialises to itself byte for
   byte. Reload into a triplestore MUST replace rather than duplicate an existing
   profile for the same sample and extractor version — this is the test that catches an
   accidental reversion to blank nodes.

---

## Appendix A — worked example output

```turtle
@prefix sp:   <http://purl.org/stuff/sampleprofile/> .
@prefix gm:   <http://purl.org/stuff/gm/> .
@prefix prov: <http://www.w3.org/ns/prov#> .
@prefix xsd:  <http://www.w3.org/2001/XMLSchema#> .

@prefix s:    <urn:sha256:3f7a1c...#> .

<urn:sha256:3f7a1c...> a sp:Sample ;
    sp:sourceFile   "kick_01.wav" ;
    sp:contentHash  "3f7a1c..." ;
    sp:sampleRate   44100 ;
    sp:channelCount 1 ;
    sp:bitDepth     24 ;
    sp:frameCount   18170 ;
    sp:hasProfile   s:profile-0-1-0 ;
    sp:hasClassification s:class-campione-rules-0-1-0 .

s:profile-0-1-0 a sp:AudioProfile ;
    prov:wasGeneratedBy s:extraction-0-1-0 ;

    sp:durationSeconds            "0.412018"^^xsd:double ;
    sp:effectiveDurationSeconds   "0.287000"^^xsd:double ;
    sp:attackTimeSeconds          "0.00400000"^^xsd:double ;
    sp:logAttackTime             "-2.39794"^^xsd:double ;
    sp:temporalCentroidNormalised "0.183400"^^xsd:double ;
    sp:crestFactorDb              "6.21000"^^xsd:double ;
    sp:decaySlopeDbPerSecond      "-139.400"^^xsd:double ;
    sp:decayFitR2                 "0.941000"^^xsd:double ;
    sp:peakDbfs                  "-0.300000"^^xsd:double ;
    sp:rmsDbfs                  "-12.4400"^^xsd:double ;

    sp:spectralCentroid s:stat-spectralcentroid ;
    sp:spectralFlatness s:stat-spectralflatness ;
    # ... remaining seven frame statistics ...

    sp:attackCentroid           "412.300"^^xsd:double ;
    sp:tailCentroid              "78.4000"^^xsd:double ;
    sp:attackTailCentroidDelta  "333.900"^^xsd:double ;
    sp:dominantPartialFrequency  "58.3200"^^xsd:double ;
    sp:dominantPartialSalience    "0.721000"^^xsd:double ;

    sp:bandEnergy s:band-0, s:band-1, s:band-2, s:band-3,
                  s:band-4, s:band-5, s:band-6, s:band-7 ;

    sp:envelopeSegment s:env-0, s:env-1, s:env-2, s:env-3, s:env-4,
                       s:env-5, s:env-6, s:env-7, s:env-8, s:env-9 ;

    sp:interChannelCorrelation "1.00000"^^xsd:double ;
    sp:midSideRatioDb         "96.0000"^^xsd:double ;

    sp:silent false ; sp:clipped false ;
    sp:multipleOnsets false ; sp:sustained false ; sp:phaseWarning false .

s:extraction-0-1-0 a sp:Extraction ;
    prov:used            <urn:extractor:sampleprofile/0.1.0> ;
    prov:generatedAtTime "2026-08-24T10:14:03Z"^^xsd:dateTime ;
    sp:specificationVersion "0.1.0" ;
    sp:fftSize 1024 ; sp:hopSize 256 ;
    sp:windowFunction "hann" ; sp:downmixMode "mean" .

s:stat-spectralcentroid a sp:FrameStatistic ;
    sp:mean "143.020"^^xsd:double ; sp:stdDev "61.2400"^^xsd:double .
s:stat-spectralflatness a sp:FrameStatistic ;
    sp:mean "0.0910000"^^xsd:double ; sp:stdDev "0.0402000"^^xsd:double .

s:band-0 a sp:BandEnergy ; sp:index 0 ;
    sp:lowerFrequency "20.0000"^^xsd:double ; sp:upperFrequency "60.0000"^^xsd:double ;
    sp:value "0.610000"^^xsd:double .
s:band-1 a sp:BandEnergy ; sp:index 1 ;
    sp:lowerFrequency "60.0000"^^xsd:double ; sp:upperFrequency "120.000"^^xsd:double ;
    sp:value "0.280000"^^xsd:double .
# ... bands 2-7 ...

s:env-0 a sp:EnvelopeSegment ; sp:index 0 ;
    sp:rmsDb "0.00000"^^xsd:double ; sp:highBandRmsDb "0.00000"^^xsd:double .
s:env-1 a sp:EnvelopeSegment ; sp:index 1 ;
    sp:rmsDb "-6.40000"^^xsd:double ; sp:highBandRmsDb "-14.2000"^^xsd:double .
# ... segments 2-9 ...

s:class-campione-rules-0-1-0 a sp:Classification ;
    sp:instrument  gm:AcousticBassDrum ;
    gm:noteNumber  35 ;
    sp:confidence  "0.910000"^^xsd:double ;
    sp:rank        0 ;
    sp:evidence    sp:FilenameToken , sp:SpectralProfile ;
    prov:wasAttributedTo <urn:classifier:campione-rules/0.1.0> .
```
