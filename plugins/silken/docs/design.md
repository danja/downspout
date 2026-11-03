# Silken design notes

Original design; the filters are the standard RBJ cookbook biquads. Times, ratios and corner frequencies are typical values chosen by ear.

## Signal flow
rumble filter -> plosive guard -> gate -> harshness tamer -> de-esser -> leveller -> mud -> warmth -> presence -> air -> drive -> sparkle -> output. Per-channel filters, one detector per stage linked across
the channels (the louder channel drives both), so the stereo image does not move. No latency: the detectors have no look-ahead, so the attack is
0.3 ms (de-esser) and 1 ms (plosive), which lets a very fast transient through for a moment.

## Plosive guard
Detector: a 2nd-order low-pass at 150 Hz, peak follower (1 ms attack, 60 ms release), 6:1 above the threshold with a 6 dB soft knee, at most 24 dB.
Action: `out = in * g + highpass150(in) * (1 - g)`. A first version subtracted the gain-reduced low band from the input; a 2nd-order low-pass has a
phase shift at 60 Hz that leaves the high band at 0.58 of the thump, so it could only take 5 dB off. Fading to a high-pass removes the low end.
Consequence: the effect on the thump is a little deeper than the metered gain reduction, and is not monotonic below a -30 dB threshold (the phase
of the cross-fade); the meter shows `g`.

## De-esser
Detector: two cascaded band-pass filters (Q 1.5 each, 0 dB at the centre), peak follower (0.3 ms attack, 20 ms release), 4:1 above the threshold with
a 6 dB soft knee, at most Range. Two stages make the detector ignore a loud vowel formant at 1-2 kHz (one stage let a -6 dBFS 1 kHz tone move the
gain by 3 dB). Band mode: `out = in - (1 - g) * bandpass1(in)`, which is exactly `g` at the centre and 0 dB far away. Wide mode: `out = in * g`.
Listen: the output is the detector (both band-passes), with the EQ and sparkle bypassed. Gain only moves up at 1 ms, so a release never clicks.

## Gate, harshness tamer, leveller
- Gate: peak follower on the louder channel (1 ms attack, 80 ms release; the release is its hold), a 2:1 downward expander with the same 6 dB soft knee, at most Range;
  the gain closes over 40 ms and opens over 2 ms. (A separate 30 ms hold was tried and removed: the envelope release already outlasts it, and no test could see it.)
- Harshness tamer: the de-esser's mechanism (`DynBand`: two band-pass detectors, a Band cut) at 1.5-5 kHz, 3:1, with Amount as its range; 0 is off.
- Leveller: peak detector (0.5 ms attack, 5 ms release) on the louder channel, soft-knee compressor, then the reduction itself smoothed (8 ms attack, 150 ms release).
  Automatic make-up is half the reduction at full scale (`-threshold * (1 - 1/ratio) * 0.5`), so a quiet passage is lifted by up to ~7 dB at the defaults; Mix blends in parallel
  as `1 + mix * (gain - 1)`. The 5 ms detector release droops between the peaks of a low tone, so the measured reduction is about 0.7 dB under the static curve.
- Drive is `tanh(a x) / a`, `a = 1 + 2 drive`; the fundamental and third harmonic are tested against a numerical integral.

## Voice EQ and sparkle
RBJ low shelf, peaking bell and high shelf, 20 ms smoothed gains, coefficients remade every 16 samples while something moves. A band at exactly 0 dB is skipped.
Sparkle: `high = in - lowpass5k(in)`, `new = tanh(6 high)/6 - high` (only the distortion products), `out += sparkle * 1.5 * (new - lowpass8k(new))`. The harmonics
are not oversampled, so high input at 8 kHz and above can alias; the 8 kHz high-pass keeps the fundamental region out.

## Idle behaviour
Both states of a biquad are flushed together below 1e-20. Flushing them one at a time left a section with |a1| > 1 (the 5 kHz low-pass) in a limit cycle
at the threshold, a permanent 1e-36 tail.

## Not built
Look-ahead; a breath reducer; mid/side de-essing; a wider dynamic EQ (resonance suppressor); formant-aware sibilant detection; oversampled drive and sparkle; a
vocal doubler / stereo widener, pitch correction, reverb and delay (separate plugins: Retune, Chorale, LightVerb, Loopdelay already exist; see TODO.md for the rest).

## Tests
`tests/silken_core_tests.cpp`, 301 checks: neutral and exact output gain; the rumble filter against the Butterworth formula at four corners and four sample
rates; the plosive guard against a hand-computed transfer function; the de-esser against hand-computed reductions, the range cap, frequency tracking, vowel rejection
(output and meter), Band against Wide, Listen, linking (either channel), attack and release; the gate (range, 2:1 law, linking, timing), the harshness tamer, the leveller (curve, make-up, mix, attack and release, linking), mud, drive against a numerical integral; the EQ gains and glide; sparkle harmonics and linearity; every control audible;
bit-exact block-size independence including automation; idle decay to exactly zero; garbage parameters; every sample rate. Mutation-checked.
