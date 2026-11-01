# Prism

Banded spectral delay. The sound is split into 1-8 frequency bands and each band is delayed
separately on the left and right, with its own feedback. Delays glide between band centres, so
there are no steps.

Magneto look: header, accent-banded panels, a plot of delay against frequency, a table of
bands (solo, left delay, right delay, feedback) and a column of spectrum controls.

## Signal flow

```
in L/R -> STFT -> per-bin delay (whole frames + phase rotation) + feedback -> ISTFT -> mix -> out L/R
              dry path delayed by N samples ----------------------------------------^
```

Each channel is processed independently; the only thing they share is the band table.

## Controls

| Control | Meaning |
|---|---|
| Bands | 1-8 active bands. Centres are evenly spaced in log frequency from 31.25 Hz to 16 kHz. One band delays everything alike |
| Left / Right delay | 0-2000 ms per band, or a note division of the host tempo (below) |
| Tempo sync (ms / RATE, per side) | RATE: each band picks a division: 1/64, 1/32, 1/16T, 1/16, 1/16D, 1/8T, 1/8, 1/8D, 1/4T, 1/4, 1/4D, 1/2, 1/1. A beat is a quarter note of the host's BBT tempo (120 BPM if the host gives none). A division longer than 2 s at the current tempo is limited to 2 s and flagged on the panel |
| Feedback | 0-95 % per band. Each repeat is delayed by the full delay |
| S (solo) | Hear only the delayed sound of the soloed bands; Mix is overridden to 100 % |
| Link L/R | The right side follows the left: delay, sync mode and division |
| Resolution | FFT size 512-4096. Sets latency and the time/frequency trade-off |
| Low Cut, High Cut | Nothing beyond the cutoff is delayed (a third-of-an-octave ramp). Off at the end of the range |
| Mix | Dry / delayed |

## Documented assumptions

- **State.** None. Every setting is a parameter (band parameters are `b1_delay_l` ...
  `b8_solo`), which DPF saves by symbol. Activation and sample-rate change clear audio state only.
- **Transport.** Only the host BBT tempo is read (`beatsPerMinute`); position, loop and play state are not used, so synced delays are a delay of a given length, not a grid-locked tap. Tempo changes glide through the 100 ms band smoothing. **MIDI.** Not used.
- **Latency.** Equal to the FFT size, reported to the host, and changed by Resolution.
- **Origin.** The technique follows the published description of the [Multiway](https://github.com/mustafamazi/Multiway) plugin
  (AGPLv3). This is an independent implementation; no code was copied.

## Known limits

- Delays between band centres are approximate: a tone halfway between two centres was
  measured within about 12 ms of the interpolated value for a 200 ms span.
- Feedback repeats lose level at some delay times: the sub-hop part of the delay costs
  `cos(pi r / N)` per trip round the loop. Measured worst case over a delay sweep: about 0.5,
  1.2 and 2.3 dB below ideal at the second, third and fourth repeat.
- Synced delays are note lengths at the current tempo; they are not phase-locked to the bar grid, and a tempo ramp moves repeats already in the loop only as they are re-read.
- A fractional-sample delay spreads a sharp transient slightly (it is not a sample-accurate delay).
- Per-bin delays make neighbouring bins phase-incoherent, which can sound metallic on
  sustained tones at long delays.

See `docs/design.md` for the algorithm and the measurements.
