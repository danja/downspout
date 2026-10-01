# keyframe — design notes

Implementation of **Keyframe Time Stretching via Extrema Sampling**
(Matthew Nielsen, DAFx26-26, Cambridge MA, 1–4 September 2026). Source paper
and README live at `~/github/dafx26-paper`; this document records what the
plugin does with the method, and every place the real-time wrapper departs from
the paper's offline formulation.

## The method in three stages

**Analysis** (paper Algorithm 1) reduces each input channel to a set of
timestamped local extrema — "keyframes" — whose spacing encodes local
information density. A bass note produces few widely spaced extrema; a cymbal
produces many tightly packed ones. This is the representation the rest of the
engine works on, and the sparse buffer is the only thing playback ever reads.

**Reconstruction** (Algorithm 2 / eq. 11) interpolates a continuous signal back
from the keyframes with a non-uniform cubic. Because every saved extremum has a
derivative of zero by construction, the Hermite tangents are zero, so both
tangent terms vanish and the spacing between keyframes drops out of the
interpolation entirely. The result is a smoothstep between two keyframe values:
C1-continuous, two multiplies, no division.

**Time stretching** (Algorithm 3) runs three playheads over the sparse buffer:

- `ϕref`, the reference, advancing at the time rate τ — where playback should be;
- `ϕplay`, the audio playhead, advancing at the pitch rate σ;
- `ϕtemp`, a temporary playhead that crossfades in during a splice.

When the two main playheads drift more than K keyframes apart, the temporary
playhead starts at the reference and crossfades over a span of exactly K
keyframes. When the crossfade ends the playhead adopts the temporary position.
The reference is the jogger and the audio playhead is the leashed dog: the dog
runs ahead or lags, but only to the end of a leash K keyframes long, and the
leash stretches and contracts with the signal because it is measured in
keyframes rather than samples. That is what makes the splice short into a
transient and long through a sustained passage, with no explicit transient
detector anywhere.

## The two rates are independent

The paper's Figure 4 uses a pitch rate of 1.65 with a time rate of 0.84, so the
two controls are genuinely independent and the pitch ratio is σ/τ:

| Control | Range | Meaning |
|---------|-------|---------|
| Time    | 0.25–1.00 | τ, input samples consumed per output sample |
| Pitch   | 0.25–4.00 | σ, playhead read rate |

At `τ = 1, σ = 1` nothing splices and the output is a straight reconstruction of
the input. At `τ = 1, σ = 2` the playhead outruns the reference, splices absorb
the drift, and the fundamental doubles at unchanged duration. At `τ = 0.5,
σ = 1` the reference falls behind and the material loops: duration stretches,
pitch stays put.

**Time stops at 1.00.** A live input stream cannot be consumed faster than it
arrives, so no setting above unity can work in real time. The parameter clamps
rather than pretending.

## Analysis details

The derivative is the quadratic B-spline kernel differenced, which gives a
three-tap FIR with a zero at Nyquist — that zero is what suppresses the
noise-floor ripples a naive finite difference would report as spurious extrema
(paper §2.1). The kernel is centred on sample n−1 so its zero crossing falls
between samples n−2 and n−1, which is where the subsample position formula puts
it.

Two details in this implementation are not in the paper and were both needed to
make the real-time case work:

1. **The sign is latched, not instantaneous.** The derivative passes through
   exactly zero at every extremum, so its raw sign goes positive → zero →
   negative across three samples. Testing the raw sign misses every crossing, and
   the sparse buffer degenerates to block boundaries alone — which sounds like a
   1 kHz-sampled version of the input rather than a time stretcher. The last
   non-zero sign is latched instead, and a change of latch is the crossing.

2. **The amplitude deadband reference follows every written keyframe.** The paper
   measures a candidate against the last *saved* extremum. Advancing that
   reference only on acceptance leaves it pointing at an older keyframe, and the
   same candidate then fails the threshold and is reconsidered on every
   subsequent crossing, producing a periodic spurious keyframe at a fixed phase
   offset from the true extrema.

A keyframe is forced at block boundaries when none has been written for a
quarter of the block (paper §2.7). Without this the buffer is empty during
silence and playback has no window to read. It is written only when genuinely
needed: forcing one where extrema already cover the instant would interpolate
across the samples either side of a point where the signal is curving, which is
precisely where it would kink the reconstruction.

**Stereo.** The paper analyses one signal. This plugin analyses the mid channel
so both channels share one sparse time base, and reads the left and right values
at each keyframe position. Analysing each channel separately would let the two
sparse time bases drift apart and the stereo image would wander across a splice;
one shared time base keeps the image locked.

## Latency and depth

Playback starts only once the analysis holds a full depth window. Before that
the reference is pinned at zero, and letting the playhead run during the warm-up
would leave it at the write head with a full window of drift, firing a splice
immediately.

From then on the reference advances at τ from position 0, so at unity it tracks
the write head exactly `kLatencySamples` (8192) behind it. That fixed offset is
the reported latency, and the dry path is delayed by the same amount so dry and
wet align. Nothing else changes the latency: it is a pure function of
`setLatency()` and never moves mid-stream.

Slowing down widens the gap between the reference and the write head, which is
the whole point — but a live stream cannot back up without bound. At
`kMaxDepthSamples` (65536, about 1.4 s at 48 kHz) the reference rides the floor
and the engine loops the material it holds. This is the honest consequence of
asking a live input for unbounded stretch, and it is the same behaviour the paper
reports in its own limitations: long periods of sparse keyframes followed by a
transient give very long splices and audible repeats (paper §3.7). `Max Splice`
is the user-facing cap on that.

The sparse buffer is a power-of-two ring of 131072 keyframes (about 2 MB), sized
for the worst case of one keyframe per input sample across the maximum depth
plus the longest splice lookahead.

## Splices

Splice length is the time span of K keyframes ahead of the reference, clamped to
`Max Splice` and to a 128-sample floor (a shorter crossfade would be a click
rather than a fade). The splice phase advances at the pitch rate, so a crossfade
of L input samples takes L/σ output samples.

The playhead is bounded only by the write head — never by the reference. At a
pitch rate below unity the playhead legitimately falls *behind* the reference,
and that negative drift is exactly what a later splice corrects. Clamping it
against the reference pins it there and silently turns every pitch setting into
the time setting, which is a failure that sounds plausible and is not.

`Hold` freezes the reference. The playhead keeps moving at the pitch rate, every
splice pulls it back to the same point, and a passage sustains indefinitely.