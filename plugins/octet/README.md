# Octet

Eight-band parametric equalizer, stereo, MIDI in for Drift (below), no latency. A redesign inspired by the EQ
Eight in `~/github/oxide-studio` (`oxide-fx`, MIT/Apache-2.0), not a port: see `docs/design.md`
for what was wrong in the source and what changed.

The panel plots the exact digital response of every band and of the total, with a draggable
node per band: drag across for frequency, up and down for gain, scroll over the plot for Q. The
curve is computed from the coefficients the audio uses, and the tests hold it to the measured
filter within 0.15 dB.

| Control | Range | Default |
|---|---|---|
| Mode | Stereo / Left-Right / Mid-Side | Stereo |
| Output | -12 to +12 dB | 0 dB |
| Band on | Off / On | bands 2-7 on, 1 and 8 off |
| Band type | HP 12, HP 48, Low shelf, Bell, Notch, High shelf, LP 12, LP 48 | HP 48, low shelf, 4 bells, high shelf, LP 48 |
| Band frequency | 20 Hz to 20 kHz (logarithmic) | 30, 100, 250, 600, 1.5 k, 4 k, 8 k, 18 k |
| Band gain | -18 to +18 dB (bell and shelves) | 0 dB |
| Band Q | 0.1 to 18 (bell, shelves, notch; logarithmic) | 1 (shelves 0.71) |
| Band channel | Both / A / B (Left, Right or Mid, Side) | Both |

Every band is at 0 dB and the two cuts are off, so a fresh instance passes the signal unchanged
bit for bit. The 48 dB cuts are 8th-order Butterworth (-3 dB at the cutoff). Settings are ordinary
automatable parameters (52 of them: `band1_freq` ... `band8_channel`, `mode`, `output`, `cc_channel`, `cc_band`); there is no
plugin state. Frequency, gain and Q glide to a new value over a few milliseconds, so fast sweeps do not zipper.

**MIDI control (Drift convention).** CC 1 to 4 on the **CC channel** (default 1; 0 ignores MIDI) drive the **CC band** (1-8, default 4): CC 1 its Frequency
(logarithmic, 20 Hz-20 kHz), CC 2 its Gain (+-18 dB), CC 3 its Q (logarithmic, 0.1-18), and CC 4 the Output. A controller holds its control until you move it on
the panel or automate it; changing the CC band releases them all. The panel does not follow the CC. No spectrum analyser yet.
