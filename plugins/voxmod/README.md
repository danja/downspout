# Voxmod

Combined vocoder and ring modulator, four inputs, two outputs.

## Routing

| Port | Purpose |
|---|---|
| in 1/2 | **Carrier** — the signal that gets analysed |
| in 3/4 | **Modulator** — the signal that gets coloured |
| out 1/2 | Processed result |

The two engines run in parallel and `Mix` blends them:

- **Mix 0%** — pure ring modulation. The modulator is multiplied by the
  internal oscillator at `Ring Freq × Ring Ratio`.
- **Mix 100%** — pure vocoder. The carrier is split into a filter bank; each
  band's envelope drives the same band of the modulator, so the modulator
  inherits the carrier's spectral shape.

`Carrier = internal osc` makes the plugin analyse its own oscillator, and
`Sync = ring → vocoder` feeds the ring-modulated signal back as the vocoder's
carrier. With both, a modulator alone on 3/4 is enough to make sound.

## Controls

- **Ring Freq** — oscillator frequency, 5 Hz to 5 kHz
- **Ring Ratio** — transposes the carrier in semitones, quantised to 16 steps
  from −12 to +35, so a move changes the sidebands musically and repeatably
- **Ring Shape** — sine, triangle, saw or square, band-limited with polyBLEP so
  the shapes do not spray aliases across the modulator
- **Ring Depth** — scales the oscillator toward unity, so 0% is a clean
  pass-through
- **Bands / Spread** — filter bank size and how many octaves it covers
- **Attack / Release** — envelope follower, time-based so the response does not
  change with the host's block size
- **Formant** — transposes the modulator side of the bank only, which moves the
  formants without moving the band grid
- **Tilt** — per-band gain in dB/oct, leaning the spectrum
- **Width** — steers the two engines apart; the vocoder is inherently mono, so
  widening pushes the ring mod out and leaves the vocoder centred
- **Drift** — a slow ±40 cent wobble on the carrier at full depth

## Randomise

Draws carrier frequency, ratio, shape, band count, spread, formant and tilt from
plausible territory, rounded so the values read as deliberate. `Mix` and
`Bypass` are preserved so a sweep does not jump the balance. Deterministic from
the `Seed` parameter.

## MIDI CC

Five CC lanes drive Mix, Ring Freq, Ring Ratio, Bands and Carrier, on a
selectable MIDI channel. CC 1-4 are Drift's default lanes; CC 5 continues the
block. Setting a lane's CC number to 0 disables that override.

See `docs/design.md` for the DSP details and `profile.ttl` for the plugin
profile.
