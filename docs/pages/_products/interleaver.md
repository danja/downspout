---
title: Interleaver
order: 229
bundle: interleaver.vst3
kind: Audio effect
category: processor
role: Two-input interleaver
screenshot: /assets/plugins/interleaver.png
capabilities: [audio input, audio output]
summary: Weaves two stereo inputs into one, by slicing them in time (free or locked to the tracked pitch of either input) or by trading phase-vocoder bins.
---

## Opinion

Not yet auditioned in a host. It is deliberately more strange than useful: the goal was "interesting" rather than "musical".
Fused with a pitched source on one side, Time mode with Track A makes a formant-ish stutter that follows the note; at the
shortest slice it becomes a sample-by-sample zipper with sidebands. Spectral mode with Bins and a small width is a comb of
two spectra; Loudest is a spectral gate that lets each source win where it is strongest.

## Functionality

Input **A** is channels 1/2 and input **B** is channels 3/4 (a second bus; in REAPER, send a track to channels 3/4 of the
Interleaver track). The output is stereo.

### Time

The two inputs are cut into alternating segments of one cycle: A for **Balance** percent, then B. The cycle is twice the
**Slice** (0.02-200 ms), or, with **Sync** on Track A or Track B, twice **Periods** times the fundamental period found on that
input by autocorrelation (the Slice is used when no pitch is found). **Edge shape** and **Crossfade** choose how the join is made:
hard cuts, a linear ramp or a raised cosine, all equal-power. **Jitter** varies each cycle at random, and **Stereo split**
delays the right channel's pattern by up to half a cycle so that left and right take opposite sources.

### Spectral

A 2048-point phase vocoder (4x overlap). **Bins** takes whole bins from A or B in groups of **Bin width**; **Mag/Phase** takes each group's
size from one source and its phase from the other, swapping every group; **Loudest** picks the louder source in every bin, tilted by Balance
by up to 24 dB. **Drift** rotates the mask across the spectrum.

### Display

The Pattern panel draws the mask the engine is using, from the same functions: three cycles of the time pattern (A up in amber, B down in teal) or
the first 128 bins of the spectral pattern. The panel header and the Time panel show the tracked pitch and cycle length as the plugin reports them.

### Status

Core covered by about 180 checks: exact pass-through at Balance 0 and 100, share and cycle length, edge continuity, pitch accuracy and
fall-back, latency in both modes, every control audible, spectral reconstruction, tone survival and rejection per bin, stability under random
parameters and at 44.1 to 192 kHz. Not yet listened to or validated in a host.
