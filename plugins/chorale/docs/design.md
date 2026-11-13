# Chorale design notes

## Provenance
- **Modes I, I+II, II** follow the junologue-chorus emulation by Peter Allwin (MIT, https://github.com/peterall/junologue-chorus) as ported to JSFX
  in `JuKnow Chorus.jsfx` (MIT). The algorithm and constants (two triangle LFOs at 0.513 and 0.863 Hz, delay ranges 1.54-5.15 ms left and
  1.51-5.40 ms right, pre-filter 2-23.5 kHz and post-filter 6-23.5 kHz on a squared Tone law, the soft limiter, equal-power mix, 20 ms smoothing)
  are that design's; the code is a fresh C++ implementation. Copyright 2020 Peter Allwin, MIT; the notice is kept here and in the source header.
- **Custom** is Downspout's own.

## Signal flow
Mono input: `(L + R)`; Stereo input (default): each side separately at `2 x`. -> soft limiter `x(27 + x^2)/(27 + 9x^2)` -> one-pole low-pass -> delay line (8192 samples; one per side in Stereo input) -> two taps per voice (the left tap reads the left line, the right tap the right line,
linear interpolation) -> Width -> one-pole low-pass per side -> `dry * sqrt(1 - Mix) + wet * sqrt(Mix)`.

- Juno: mode I uses sweep 1, II sweep 2, I+II both at 1/sqrt(2). The left tap is `T0min + (T0max - T0min) p`, the right `T1min + (T1max - T1min)(1 - p)`
  with `p = |triangle|`, so the two sides move in opposite directions.
- Custom: voice `v` of `V` has phase `phase + v / (2V)`; left delay `Delay + Depth sin(2 pi phase_v)`, right `Delay - Depth sin(...)`; the voices are summed at `1/V`.
  The sweep is held below Delay so the delay never goes negative. Delay positions follow the source's convention (read position 1 is the newest sample).
- Mode changes crossfade over 20 ms. Rate, Depth, Delay, Width and Mix glide on 20 ms, so automation and controllers do not zipper.
- Width: `wet = mid + Width (wet - mid)` per side.

## Assumptions (LV2/JSFX -> DPF/VST3)
- State: all settings are parameters (no `WANT_STATE`). A controller takeover is runtime only and is released when the host or panel sets that control.
- Activation clears the delay line, filters and LFOs, never the settings.
- Controllers: CC 1 Rate (logarithmic), CC 2 Depth, CC 3 Width, CC 4 Mix, on the CC channel. The panel shows the value in force with "(CC)".
- Input: Mono sum is the source behaviour and is exact against the reference. Stereo (the default) gives each side its own line at twice its input, so a
  centred signal is bit-identical to Mono and a hard-panned one stays on its side. Both are +6 dB wet for a centred signal, as the source is; the soft limiter
  adds a little saturation to loud input.
- The default mode is I+II (the source defaulted to I).

## Not built
Feedback (flanger), a vibrato-only (100 % wet) switch, higher-order interpolation, oversampling the limiter. Tempo sync is available via the Sync toggle and scales Rate with the host BBT tempo.

## Tests
`tests/chorale_core_tests.cpp`, 205 checks: the Juno modes against an independent straight-line transcription of the source algorithm (max error ~4e-8);
dry path and mix law; Custom delay, depth, rate and voice spread measured by correlation against the input; width, tone, modes and every control having an effect;
the Custom controls inert in the Juno modes; click-free mode changes; controllers (channel, number, sample accuracy, release, status); bit-exact block-size
independence; every sample rate down to the longest delay; idle decay to exact zero; garbage parameters. Mutation-checked.
