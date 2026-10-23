# Ladder: design notes

Source: `~/github/oxide-studio/crates/oxide-dsp/src/filter.rs` (`MystranZdfLadder`, MIT/Apache-2.0),
a 2x-oversampled non-linear zero-delay-feedback 4-pole low-pass ladder with a Pade [4/4]
`tanh(x)/x`. Its README overstates other devices, so this one was measured before porting.

## What was measured on the source (literal C++ port, 48 kHz)

- Resonance 0: gain matches four cascaded one-poles, `-40 log10(1 + (f/fc)^2)` dB, to 0.01 dB at
  200 Hz and 1 kHz (-12.04 dB at the cutoff, -3 dB at about 0.44 fc). At 5 kHz it is steeper
  (-12.3 dB at fc, -58 dB at 4 fc) from the prewarping.
- Self-oscillation: sustained, bounded (about 0.17 peak), within 0.5 % below the cutoff from
  100 Hz to 8 kHz. No NaN through random cutoff/resonance sweeps at 44.1 and 96 kHz.
- Bass loss: DC gain is `1 / (1 + k)`. The source only restores `1 + 0.25 * resonance`, so
  resonance 0.5 loses 9.5 dB at 100 Hz.
- Taper: `k = 4.35 * res^0.85` puts the peak at +0.9 dB at 0.5 but +30 dB at 0.9, so almost all
  the useful range is in the last tenth of the control.
- Loud input at the resonant peak compresses (about 3 dB at 1 kHz at 0.001 input to -17 dB at 2.0).
  That is the saturator, and it is kept.

## What differs from the source

- **Resonance taper**: `k = 4.2 * (1 - 10^-u) / 0.9`. With full bass compensation the peak is
  2, 6, 10, 18, 28 dB at 10, 20, 30, 50, 70 %, and self-oscillation starts near 85 %.
- **Bass compensation** is a control (default 50 %): the output is multiplied by `1 + comp * k`,
  so 100 % restores the passband to 0 dB. At full compensation the resonant peak is loud.
- Added Drive (pre-filter gain into the saturators), Mix and Output.
- k and f are interpolated linearly across each block; the first block snaps to its target.
- Tiny states are flushed to zero once per block (denormals), and a non-finite output resets
  that channel.

The stage-gain indexing in the source (`f1` built from `g1` and `f2`, and so on) was not
re-derived. It is validated by the response and stability results above, not by a derivation.

## Not done

- Not yet validated in a host (REAPER): automation of Cutoff, save and reopen, sample-rate change.
- Drift CC input is in (CC 1-4 on a channel: Cutoff, Resonance, Drive, Mix; a controller overrides its control until the panel moves it); the panel does not follow it. The mapping and override logic are core functions with tests (104 checks in all).
- Not listened to. CPU measured on a busy machine (load about 30, so an upper bound): about 5.5 % of one core
  for stereo at 48 kHz (-O2), the same
  through a 60 s silent decay after a loud burst, so the denormal flush works.
- Aliasing from the saturators at high drive is not measured; the 2x oversampling is the
  source's, with linear midpoint interpolation of the input.
