---
title: Trio
order: 221
bundle: trio.vst3
kind: Audio effect
category: processor
role: Multiband dynamics
screenshot: /assets/plugins/trio.png
capabilities: [audio input, audio output]
summary: Three-band dynamics with adjustable flat-summing crossovers, per-band downward compression and upward lift, Time and Amount macros, and live per-band gain bars.
---

## Opinion

Not yet auditioned in a host. The defaults are gentle (2 : 1 down, 1.5 : 1 up); raise the ratios to 4 : 1 and 2.5 : 1 for the well-known "OTT" sound, a strong, bright, dense effect
good on drums, synths and vocals you want forward. Set an Up ratio below 1 to use a band as a downward expander instead of a lift. Set every ratio to 1
for a plain three-band split. It is a redesign, not a copy of the Oxide Studio device that inspired it, which gives the left and right channels
of each band one shared envelope and steps it twice per sample.

## Functionality

The input is split at two crossovers into low, mid and high bands that sum back flat. Each band watches its own level (the louder of the two
channels) and does two things: above the **Down threshold** it compresses by the **Down ratio**, and below the **Up threshold** it lifts by the
**Up ratio**, up to the **Upward limit**. Between the thresholds it does nothing. The gain is smoothed with the band's **Attack** and **Release**,
both scaled by **Time**.

### Parameters

| Parameter | Range | Default |
|---|---|---|
| Low / high crossover | 40-1000 Hz / 1-12 kHz | 250 Hz / 2.5 kHz |
| Input | -12 to +24 dB | 0 dB |
| Output | -24 to +12 dB | 0 dB |
| Amount | 0-100 % | 100 % |
| Time | 0.1-10 x | 1 |
| Upward limit | 0-24 dB | 18 dB |
| Band On | Off / On | On |
| Down threshold / ratio | -60 to 0 dB / 1-20 : 1 | -16 / -14 / -12 dB, 2 : 1 |
| Up threshold / ratio | -80 to 0 dB / 0.2-10 : 1 (below 1 = downward expander) | -40 / -38 / -36 dB, 1.5 : 1 |
| Attack / release | 0.5-200 ms / 5-2000 ms | 10 / 100, 5 / 80, 2 / 50 ms (low, mid, high) |
| Band gain | -18 to +18 dB | 0 dB |

### The panel

The band plot has a draggable line for each crossover. Each band shows a bar for the gain it is applying now (down from the centre line for
reduction, up for lift), its value in dB, and its level. The bars and levels are measured from the audio.

### Notes

- Input drives the bands only; the dry path (heard at Amount below 100 %) is untouched, and Amount 0 is bit-exact dry.
- The upward lift raises the noise floor by up to the Upward limit, and there is no gate.
- The two crossovers are kept at least 1.25 x apart.

### Status

Core covered by tests (303 checks): the crossover sums flat at several settings and sample rates, the gain formula in each band, band
independence, the stereo link, attack and release time constants and the Time scaling, Amount, Input and Output, the upward limit, band on and off,
meters, every control, stability and overload at 44.1 to 192 kHz, block-size independence, denormals, and the parameter table against the core. Not yet
listened to or validated in a host. Takes Drift CC 1-4 on a channel (Amount, Time, Input, Upward limit). No look-ahead. **Knee**, **Solo** (a band's processed signal alone) and an external **sidechain** (inputs 3-4, with the detectors on the same three bands) are on the Global panel.
