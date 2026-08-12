## Downspout integration plan

Algorithm credit: u/Lanky_Scarcity6223 (r/synthdiy). Original post reproduced below.

### Context

The core insight — that direction + three inter-crossing intervals (d₁ d₂ d₃) pins the exact position within a waveform cycle — is directly useful for Downspout's sample-playing and loop-capture plugins. A single zero-crossing search finds a sample value of zero; the stencil finds the *right* zero in the cycle.

### Plugin targets

**Mosaic** (implemented)
Mosaic selects random slice regions from loaded WAV samples and plays them as one-shot grains. The randomly-chosen start frame is almost always mid-cycle, requiring a long attack envelope to mask the onset click. Applying a zero-crossing snap — preferring rising crossings, ±3 ms window around the chosen start — moves the onset to a phase-coherent point so voices begin cleanly. For reverse grains, a falling crossing is preferred instead. Implementation: `snapToZeroCrossing()` in `mosaic_core.cpp`, called from `trigger()`.

**Loop Delay** (implemented)
The loop recorder had no boundary handling at the loop-wrap point; a direct buffer-index wraparound produced audible clicks whenever the audio level at loop-end and loop-start differed. After capture completes, `snapLoopEnd()` in `loopdelay_core.cpp` searches backward within ±3 ms of the captured end for the nearest zero-crossing and trims `loopLength` there. The loop is at most 3 ms shorter than requested, which is inaudible, but the wrap is now click-free for tonal material.

**Orchid** (implemented)
Orchid already minimised a join-cost function (amplitude discontinuity) and applies a quarter-sine blend at loop wrap. `loopJoinCost()` in `orchid_engine.cpp` now adds a zero-crossing penalty (weight 0.5×) on the absolute values of the first and last samples of each candidate loop region. The existing ±32-frame search window therefore prefers loop starts and ends that are near zero, finding phase-coherent splice points rather than merely level-matched ones. This matters most for the MIDI pitch-shift feature: a loop that spans a whole number of cycles plays back in tune; a half-cycle error shifts the pitch by a fraction of a semitone per pass.

---

## Original post — r/synthdiy, u/Lanky_Scarcity6223

 If you have ever used software like Polyphone to prepare samples (e.g. for FluidSynth or embedded players), you know the pain. Finding loop points manually is tedious, and auto-loop tools usually settle for the first zero-crossing that roughly matches in amplitude. That catches a zero-crossing, but not necessarily one at the same point in the wave cycle — and the result is a click, a phase cancellation, or a loop that sounds a semitone-ish off because it is short or long by half a cycle.

To fix this for my sampled engines (Reface CP port), I wrote a small pattern-matching tool. Instead of scoring a single point, it matches the phase pattern around the loop end against the phase pattern around candidate loop starts.

diagram
The algorithm

    The stencil. The tool collects every zero-crossing in the file and takes the last one as its reference. Around it, it builds a small template: the crossing's direction (rising or falling) plus the distances back to the previous, second-previous and third-previous crossing — d₁, d₂, d₃. Four points, three intervals. Where that reference sits is up to you: the tool uses the end of the file, so you trim the input to somewhere in the stable part of the tone (around 1.5–2 s works well) before running it.

    A period estimate. d₁ — the distance to the previous crossing — becomes the unit of length, P̂. Note this is not the full cycle: any waveform has at least two zero-crossings per cycle, so for an asymmetric wave P̂ is typically the shorter half of one cycle. In the diagram, P̂ = 47 samples while the true period is 122.

    Where to look. For a candidate loop length k, the tool centres a search window at loopEnd − k·P̂ and opens it up by ±2·P̂ to both sides. Every zero-crossing inside that window is a candidate.

    Scoring. A candidate of the wrong direction is rejected outright — a rising crossing never matches a falling one, whatever the distances say. The rest get a weighted relative error:

    The nearest interval weighs three times as much as the farthest, because the crossing right next to the splice is where an error is most audible. Lowest score in the window wins.

    Choosing k. It starts at k = 10 and, if the score is worse than 0.01, walks upward to 30, stopping at the first window that clears the threshold. If nothing does, it walks back down to k = 3, and keeps the best it saw. So it isn't a global minimum over all k — it is the first window good enough to stop looking, which in practice is what you want, because every extra period is flash you pay for.

Why three intervals instead of one

A single zero-crossing carries almost no information: a 440 Hz note at 32 kHz has one every ~36 samples and they all look alike. Direction plus three intervals pins down where in the cycle you are. In the diagram's search window there are three candidates: one is a falling crossing (rejected on direction alone), one is a rising crossing that sits at the wrong point in the cycle and scores 0.064, and one reproduces d₁/d₂/d₃ exactly. Only the last one splices cleanly.

The other thing this buys you is that the loop length comes out as a whole number of cycles automatically. You never end up half a cycle short, which is the usual cause of that "the loop is slightly out of tune" feeling.
What it does not do

Worth being clear about, because it changes how you use it:

    No crossfade. The splice is a hard cut. It works because the phase matches, not because anything is smoothed over.

    No amplitude matching. Only distances and direction go into the score — never the sample values. On a sample that is still decaying noticeably, the loop start is louder than the loop end and you will hear the level step on each pass, no matter how good the score is. Trim into a region where the decay has flattened out.

    It won't rescue a bad sample. Noisy or inharmonic material produces jittery crossings and no window scores well. The tool reports that (>0.15 it says so outright) rather than pretending.

Within those limits it has been reliable for me: for the CP and MKS-20 sample sets it found scores under 0.01 for the large majority of notes, unattended.
The implementation

One C++17 file, no dependencies, reading and writing 16-bit mono WAV:

tools/cp_sampleprep/build_loop_finder.sh
tools/cp_sampleprep/FindLoopPoints <sample.wav> [num_periods]

It overwrites the input WAV in place, trimmed to the loop end, and writes the loop start to a <sample>.loop file next to it. Work on copies. In the collection it isn't run by hand — prepare_samples.py shells out to it once per sample while building the voice headers.

Source: tools/cp_sampleprep/src/FindLoopPoints.cpp in https://github.com/Michi71/PicoVintageSynthCollection

Have you built similar pattern-matching loops for your samplers, or do you still rely on manual crossfading? And has anyone tried scoring the derivative across the splice as well — I suspect that would catch the cases where the phase matches but the slope doesn't quite.
