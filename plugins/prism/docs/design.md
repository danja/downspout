# Prism design notes

## Files

| File | Role |
|---|---|
| `include/prism_params.hpp` | Parameter table (built once), `Parameters`, band geometry (`bandCentreHz`, `bandPosition`), cut weights. Portable |
| `include/prism_fft.hpp` | Radix-2 complex FFT (same as Qune's, own namespace) |
| `include/prism_engine.hpp`, `src/prism_engine.cpp` | The STFT engine. Portable |
| `src/dpf/PrismPlugin.cpp` | Thin DPF wrapper: parameters 1:1, latency reporting |
| `src/dpf/PrismUI.cpp` | Magneto UI |
| `tests/prism_core_tests.cpp` | Deterministic core tests |

## Algorithm

Per channel: a circular input FIFO of N samples, a circular overlap-add accumulator, and a
history of the last `maxFrames + 1` spectra. Every hop (N/8 samples) the FIFO is windowed
(periodic sqrt-Hann), transformed, processed and added back through the same window. The sum of
squared windows at hop N/8 is constant; its inverse is the COLA scale, computed numerically.
Output sample n of a frame lands n+1 samples later, so latency is exactly N, and the dry path is
the FIFO value N samples old.

Per bin k, with whole-frame delay F, residual r (samples, |r| <= hop/2) and feedback g:

```
delayed = F > 0 ? history[frame - F][k] : input[k]        (zero before the first frames)
delayed *= exp(-j 2 pi k r / N)                           (the sub-hop part of the delay)
history[frame][k] = F > 0 ? input[k] + g * delayed : input[k]
output[k] = delayed * solo[k] / cos(pi r / N)
```

The rotation shifts the windowed frame by r samples. Summed over overlapping frames the
analysis and synthesis windows (sin) give `sum sin(a) sin(a + b) = (overlap/2) cos(b)` with
`b = pi r / N`, so dividing by `cos(b)` restores unity. The correction is applied to the output
only: inside the loop it would raise the loop gain. For the same reason feedback repeat k is
scaled by `cos(k b) / cos(b)`; that is the level loss documented in the README, and the reason
the hop is N/8 rather than N/4 (it halves the largest residual).

Band geometry: centres at `31.25 * 2^(octaves * i / (n - 1))`, 9 octaves to 16 kHz. A bin's
position is `log2(f / 31.25) / (octaves / (n - 1))`, clamped, split into a band and a fraction;
delay, feedback and solo weight are interpolated linearly in that fraction. Cuts multiply the
delay and feedback by a weight that ramps over a third of an octave.

Tempo sync: each side has a switch. Synced, a band's delay is `division_beats * 60000 / bpm` ms
(divisions grouped by note value, so lengths are not monotonic), limited to 2000 ms; linked, the
right side takes the left's switch, division and delay. The tempo comes from the host BBT
position through `setTempo`, recomputed every hop and passed through the same 100 ms smoothing
as the milliseconds, so a tempo change glides. The panel gets the tempo through an output
parameter (`out_tempo`), since a DPF UI has no transport.

Smoothing, per hop: band values 100 ms, solo weight 30 ms; the mix is smoothed per sample (50
ms) and starts at its parameter value on the first block.

## Measurements (tests)

- Impulse through equal delays on every band arrives within 1.5 samples of N + delay at all four
  sizes for delays from 0 to 1999 ms (integer sample delays; a fractional-sample delay lowers the
  sampled peak, as any band-limited delay does).
- A 1 kHz burst keeps its level within 0.3 dB at fractional delays.
- Feedback repeat level over a delay sweep (70 % feedback, 41-420 ms, sizes 512 and 2048): worst
  error 0.01, 0.49, 1.23, 2.33 dB for repeats 1-4.
- A tone between two band centres (200 ms span) is within 20 ms of the interpolated delay (about
  10 ms short in practice): each bin's delay is realised as a phase slope that itself varies
  with frequency, so the delay in between is approximate. Above the top centre it holds exactly.
- Solo isolates the soloed band (>30x in amplitude); cuts leave their region undelayed (>10x).
- 95 % feedback on all bands stays bounded and the tail decays.
- Tempo sync: 1/8 at 120 BPM, 1/4 at 90, 1/8T at 120, 1/16 at 140 and 1/2 at 60 land within 2.5 samples of the expected time; the unsynced side keeps its milliseconds; linked follows the left; a division beyond 2 s is limited; a tempo change mid-play moves the delay to the new length.
- Output is identical at host block sizes 1, 37 and 4096.
- Speed (loaded machine, an upper bound): 512 at 96 kHz about 19 % of real time; 4096 at 48 kHz
  about 7 %.

## Not done

- LFOs / shaper and a drawn modulation shape, as in the plugin that inspired it.
- Locking synced repeats to the bar grid (only the tempo is read, not the position).
- Per-bin phase coherence (the technique is metallic on sustained tones at long delays).
- Listening tests; host validation.
