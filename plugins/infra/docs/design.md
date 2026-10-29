# Infra design notes

Infra is a new plugin, not a port. It is a low-frequency oscillator driving a stereo VCA.

## Rate

The LFO runs at `multiple x beats per second`. The beat is the host's BBT beat (a quarter note in 4/4, an eighth in 6/8) and the
tempo is the BPM the host reports; whether a host reports BPM in quarter notes or in beat units for compound time is its own
choice and is not corrected here.

- `Multiple = 0` is **Auto**: `round(10 Hz / beat rate)`, so the rate is the whole multiple nearest to 10 Hz (5 at 120 BPM,
  10 at 60 BPM, 4 at 140 BPM). Where two multiples are equally near, the higher is used.
- Every request is clamped to the multiples that keep the rate within 1-20 Hz at the current tempo (`effectiveMultiple()`),
  so the rate never leaves the range as the tempo changes. At a tempo so slow that 40 cycles per beat is still under 20 Hz the
  rate may stay below the requested one; above 1200 BPM one cycle per beat is already over 20 Hz and is used anyway.
- The multiple in use and the resulting rate are output parameters the panel shows.

## Phase and transport lock

`cycles = beatPosition x multiple + channel offset`, with the offset 0, 0.25, 0.5 or 0.75 cycle. Both channels read the same
waveform; with the default 90 degrees on both the LFO is at its peak, 0 dB, on every beat. While the transport plays the LFO
phase is computed from the host's beat position each block and advanced per sample, so it is locked and independent of block
size. When the transport is stopped (or the host sends no BBT) the phase free-runs from an accumulator, at the host tempo if one
is known, else 120 BPM. `activate()` and a sample-rate change clear only the gain smoother and the free-run phase.

## Waveforms

All are unipolar 0..1 after mapping a bipolar shape `b` (`0.5 + 0.5 b`), sine-aligned so each rises first and peaks a quarter
cycle in: sine, triangle, ramp up, ramp down, square, soft square (`tanh(4 sin)` normalised), sample & hold and smooth random.
The random shapes take a deterministic value per cycle from an integer hash of the cycle number (smooth random interpolates
between consecutive cycles with a cosine), so a given cycle always sounds the same, including after a jump in the transport.

## VCA

`gain dB = -Range x (1 - lfo)`: 0 dB at the LFO peak, `-Range` at its trough. The gain is smoothed in dB with a time constant of
0.5 ms so a square wave or a random step does not click (this slightly rounds square edges). Non-finite input becomes silence. A
mono input is not special-cased: the plugin always has two input channels.

## Assumptions and not done

- Not auditioned in a host and not tested against real host transports (REAPER); the BBT mapping to a beat position is the part
  most worth checking, including looping and tempo changes.
- No sync to a fixed rate in Hz, no fractional multiples (a slower LFO than one cycle per beat), no per-channel waveform, no
  depth modulation, no MIDI control.
- The 1-20 Hz limit is enforced on the rate, so at slow tempi the lowest usable Multiple is above 1.
