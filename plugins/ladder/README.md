# Ladder

Stereo four-pole low-pass ladder filter with a saturating, self-oscillating resonance. It is a
port of the zero-delay-feedback ladder in `~/github/oxide-studio` (`oxide-dsp`, MIT/Apache-2.0),
measured against the analytic response before porting and changed where the measurements
showed a weakness; see `docs/design.md`.

The panel plots the filter's small-signal response from the same model the tests compare with
the real filter, so the curve and the sound agree. MIDI in for Drift (below); no transport, no latency.

| Control | Range | Default | Notes |
|---|---|---|---|
| Cutoff | 20-18000 Hz (logarithmic) | 1 kHz | -12 dB at the cutoff with no resonance |
| Resonance | 0-100 % | 20 % | Peak over the passband (full bass comp): about 6 dB at 20 %, 18 dB at 50 %, 28 dB at 70 %. Rings on its own from about 85 % |
| Drive | 0-24 dB | 0 dB | Gain into the saturators; not shown in the plot |
| Bass Comp | 0-100 % | 50 % | Restores the low end the feedback removes. 100 % puts the passband back at 0 dB |
| Mix | 0-100 % | 100 % | Dry/wet |
| Output | -24 to +12 dB | 0 dB | |

At the defaults the passband sits about 3.5 dB below unity (resonance 20 %, Bass Comp 50 %).
That is the behaviour, not a bug: raise Bass Comp to bring it up.

**MIDI control (Drift convention).** CC 1 to 4 on the **CC channel** (default 1; 0 ignores MIDI) drive Cutoff (logarithmic
across its whole range), Resonance, Drive (0-24 dB) and Mix, so a Drift on its default lanes needs no setup. A controller holds its
control until you move it on the panel or automate it. The panel does not follow the CC.

Settings are ordinary automatable parameters, saved by the host; there is no plugin state.
High Resonance with full Bass Comp is loud; trim Output.
