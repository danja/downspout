---
title: Octet
order: 220
bundle: octet.vst3
kind: Audio effect
category: processor
role: Eight-band equalizer
screenshot: /assets/plugins/octet.png
capabilities: [audio input, audio output]
summary: An eight-band parametric equalizer with bell, shelf, notch and 12 or 48 dB cut bands, Stereo, Left/Right and Mid/Side modes with per-band channel assignment, and a plot with a draggable node per band.
---

## Opinion

Not yet auditioned in a host. Downspout had no equalizer, so this fills the most basic gap in the effects: cut the
rumble with a 48 dB high-pass, dip a muddy bell, lift the air with a shelf, and see all of it on the plot. It is a
redesign, not a copy of the Oxide Studio EQ that inspired it, whose "48 dB" cuts are 9 dB too deep at the cutoff.

## Functionality

Every band is one of eight types and has its own on switch. Drag a node on the plot (across for frequency, up and down
for gain) or use the controls under it; scroll the wheel over the plot to change the selected band's Q. A fresh instance
changes nothing: all gains are 0 dB and the two cut bands start off.

### Parameters

| Parameter | Range | Default |
|---|---|---|
| Mode | Stereo / Left-Right / Mid-Side | Stereo |
| Output | -12 to +12 dB | 0 dB |
| Band on | Off / On | bands 2-7 on, 1 and 8 off |
| Band type | HP 12, HP 48, Low shelf, Bell, Notch, High shelf, LP 12, LP 48 | HP 48, Low shelf, Bell x4, High shelf, LP 48 |
| Band frequency | 20 Hz to 20 kHz | 30, 100, 250, 600, 1.5 k, 4 k, 8 k, 18 k |
| Band gain | -18 to +18 dB | 0 dB (bell and shelves only) |
| Band Q | 0.1 to 18 | 1; 0.71 for the shelves (bell, shelves and notch only) |
| Band channel | Both / A / B | Both |

### Cuts

HP 12 and LP 12 are second-order Butterworth. HP 48 and LP 48 are 8th-order Butterworth, four cascaded sections with the
Q values 0.51, 0.60, 0.90 and 2.56, so they are -3 dB at the cutoff and fall 48 dB per octave beyond it. They have no gain
or Q control.

### Stereo, Left/Right and Mid/Side

In **Stereo** every band acts on both channels. In **Left / Right** and **Mid / Side** each band's *Acts on* chooses both
channels, A (left, or mid) or B (right, or side). In Mid/Side a Side band changes the stereo width at that frequency and
leaves a mono signal exactly alone.

### Precision

Filters run in double precision, so a 20 Hz 48 dB high-pass is accurate at 192 kHz (within 0.3 dB of -3 dB at the corner).

### Status

Core covered by tests (380 checks): the plotted response against the measured filter for every type at 48 and 96 kHz, band
shapes, cut slopes, the routing modes, stacking bands, output gain, every control, impulse-response decay for random
settings at 44.1 to 192 kHz, block-size independence, denormals, and the parameter table against the core. Not yet listened to
or validated in a host. No spectrum analyser, no coefficient smoothing, no linear-phase mode.
