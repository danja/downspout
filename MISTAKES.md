# Mistakes log

Open or new entries only. Resolved lessons are promoted to "Lessons from past mistakes"
in `CLAUDE.md` and removed from here. Format: what happened, root cause, prevention.

## Open

- **Screenshot script captures the real desktop.** `scripts/capture-plugin-screenshots.sh`
  only starts Xvfb when `DISPLAY` is empty, so from a session with `DISPLAY` set it
  screenshotted the user's browser (twice, 2026-10-07 and 2026-10-08, overwriting
  `pratt.png`). Prevention is in CLAUDE.md (`env -u DISPLAY`). Remove this entry once the
  script always forces Xvfb (shared glue, needs approval; tracked in TODO.md under Pratt).

## Plank: repeated note fix killed the string (2026-10-15)
- **What happened:** "Second note doesn't play" was fixed by retriggering on a repeated note-on, but the host's overlapping order (on, on, off, off) meant the first note-off released the freshly retriggered string, so output still died.
- **Root cause:** Assumed the repeat was a missing note-off; never modelled the host's on-before-off ordering. Core tests only covered on/off in separate blocks.
- **Prevention:** For note handling, test the same-tick orderings a host can send (off-then-on, on-then-off) and count overlapping note-ons per voice.

## Qune: tests that measured the test signal, not the plugin (2026-10-18)
- **What happened:** The first pitch-tracker tests failed at 260-650 Hz with octave and 3x errors, and an engine probe reported "wrong" pitch for a correct shifter. Hours went on tracker changes before the cause showed up in the generator and the expectations.
- **Root cause:** (1) The synthetic vowel was an impulse train sampled at integer positions, so its period jittered by up to a sample (1.4 % at 650 Hz): a noisy input the detector rightly struggled with. (2) Expected notes were worked out by eye: "F4 +80 c" is nearer F than G, and C#5 is in D major. (3) A probe set Amount to 0 and a fractional Transpose, forgetting Transpose is an integer parameter.
- **Prevention:** Generate test tones additively (exact period) and sanity-check the generator with the reference measurement before testing the unit. Derive expected notes in the test from the scale set instead of by hand. Re-read the parameter spec (`integer`) before writing a probe around it.

## Qune: PSOLA pitch marks (2026-10-18)
- **What happened:** The Good tier measured 7 cents off at 550 Hz, then showed 15-25 dB spectral notches when Formant Shift moved the envelope.
- **Root cause:** Grain centres were rounded to whole samples (half a sample of timing noise per mark), and the first mark was placed wherever the ideal epoch fell, mid-cycle, so neighbouring harmonics inside a grain interfered.
- **Prevention:** Keep pitch marks fractional, put the first mark of a run on a waveform peak, and normalise overlap-add by the window weight actually present. Measure output pitch against the requested pitch, and the harmonic envelope against the known vowel envelope, for every tier.

## Qune: tier switch clicked (2026-10-18)
- **What happened:** The click test failed with a 0.21 step against a 0.02 input step when Quality changed.
- **Root cause:** Only the wet signal was faded, but a quality change also changes the latency of the dry path, which then jumped.
- **Prevention:** When a parameter changes latency, fade the whole output, not one path, and test every transition (quality and range) for continuity.

## Sinevox: formant tracker read F1 12 % high on a flat test source (2026-10-20)
- **What happened:** The LPC tracker put a 700 Hz resonance at 790 Hz (Q 3.4 instead of ~8) in the first test run. Changing the window length did nothing; changing the pre-emphasis coefficient moved it.
- **Root cause:** The test vowel was an impulse train through resonators, which has a flat spectrum. The analyser assumes speech's ~-6 dB/oct tilt (glottal slope plus lip radiation) and pre-emphasises to flatten it, so on a flat source it over-tilts and biases the lowest formant upward.
- **Prevention:** Give synthetic test signals the spectral character the algorithm assumes (here a one-pole tilt on the pulse train) and say so in the generator. Vary one assumption at a time (window, pre-emphasis) in a throwaway probe before touching the algorithm. Also: a "silent after release" test must wait long enough for the release to cross the idle threshold (about 12 time constants), not 7.

## Bassops: defaults cut the bass on one channel (2026-10-20)
- **What happened:** On default settings a mono input lost bass. Measured: right channel -31 dB at 120 Hz, -67 dB at 30 Hz; left channel 6 dB down at every frequency.
- **Root cause:** (1) The right output was `LP(x) - HP(x)`, which is only the identity the notes claimed for the left one. (2) The 129-tap Hann sinc LP at 200 Hz was never normalised and summed to about 0.5, so `HP = delta - LP` passed half of the bass and the right channel cancelled to nothing at DC. (3) A mono track feeds only main input 1, so the mono sum `(L+R)/2` was 6 dB down. The existing tests only checked silence and ducking, never the default frequency response, and the doc comment asserted a property nobody measured.
- **Prevention:** Test the default signal path's gain per channel across the bass range, for stereo and for a one-sided (mono) feed. When a design note claims an identity ("LP + HP = delay"), assert it numerically for every output. Normalise windowed-sinc kernels to unity DC gain.


## Sam: assumed a private JACK server, attached to the real one (2026-10-22)
- **What happened:** To drive Sam's standalone I started `jackd -r -d dummy` and assumed it was the server the app would join. A `jack_lsp` showed the user's real devices (USB mic, built-in audio): a `jackdbus` session was already running, the dummy server had not taken over, and the standalone (and the earlier UI-driving and screenshot runs) had registered on the user's real graph. Nothing was audible, because nothing had sent MIDI yet.
- **Root cause:** Never checked which server was live before sending audio. The capture script has the same blind spot: it starts a dummy server only if none responds, and otherwise uses whatever is there.
- **Prevention:** Before a test sends audio or MIDI through JACK, run `jack_lsp` and look for real hardware ports; disconnect the app's outputs from everything but the test's own capture ports, and abort if anything else is attached (the throwaway client for Sam did this). Do not start a second `jackd` without checking.

## Sam: word onsets depended on where the host cut its blocks (2026-10-22)
- **What happened:** A word starting within a frame of the end of a block was placed on the wrong frame, so rendering the same song with block sizes 1 and 480 differed by one frame. The block-size test caught it before anything shipped.
- **Root cause:** The onset frame was `round((slotStart - rel0) / beatsPerFrame)` clamped to the block, so a boundary at .6 of a frame landed one frame early in a block that ended there and on the right frame in one that did not.
- **Prevention:** Start an event on the first frame at or after its time (`ceil`, with a tolerance for float noise) and count slots already started, so each fires once whatever the block size. Keep a block-size independence test for anything scheduled against the transport.
