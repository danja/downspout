# Spense

Stereo phase disperser. Up to eight cascades of eight second-order all-pass filters smear the
phase of a signal without changing its level: frequencies near **Frequency** are delayed by
a few milliseconds more than the rest, which typically softens transients and adds a short pitch
sweep to clicks and hits (not yet auditioned; see Status). It is a port of the DSP in `~/github/Dispenser` (MIT, JUCE); see
`docs/design.md` for what was kept and what changed.

The panel plots the stack's group delay (ms against frequency), computed from the same model the
tests compare with the real filter, so the curve and the sound agree.

| Control | Range | Default | Notes |
|---|---|---|---|
| Stack | 1-8 | 1 | Cascades in series, 8 all-pass stages each. Dispersion adds up; latency is 7 samples per cascade |
| Frequency | 40-20000 Hz (logarithmic) | 1 kHz | Centre of the dispersion |
| Resonance | 0.1-10 Q (logarithmic) | 0.707 | Low Q is a broad, gentle smear; high Q a narrow, long one that rings at Frequency |
| Drift | 0-1 | 0 | Fans the eight stages apart in frequency, differently on the left and right, for a wider, less static response |
| Pre Gain | -100 (silence) to +12 dB | 0 dB | Before the stack |
| Post Gain | -100 (silence) to +12 dB | 0 dB | After the stack |
| Clip at 0 dB | Off / On | Off | Hard clip after Post Gain |

At Q 0.707 the delay peaks at about 0.64 x Frequency, not at Frequency itself (a property of a
second-order all-pass); by Q 5 the peak is within 3 % of Frequency. The panel marks Frequency.

Status: core covered by tests (against the source algorithm, measured phase and group delay, stability,
denormals, lifecycle). Not yet listened to or validated in a host.

Stereo in and out, no MIDI, no transport. The stack's latency (stack x 7 samples) is reported to the
host. Settings are ordinary automatable parameters, saved by the host; there is no plugin state.
Level is unchanged by the stack (an all-pass has unity gain), so Pre and Post Gain only matter for
level matching, overload and the clipper.
