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
