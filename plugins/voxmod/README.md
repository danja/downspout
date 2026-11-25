# Voxmod

Vocoder, ring modulator and live convolution cross-synthesis, four inputs, two
outputs.

## Routing

| Port | Purpose |
|---|---|
| in 1/2 | **Modulator / source** — the signal that gets coloured |
| in 3/4 | **Carrier / live IR** — analysed by the vocoder, multiplied by the convolver |
| out 1/2 | Processed result |

Three engines run in parallel. `Mix` balances the first two, and `Conv Mix`
fades that pair against the convolver. Each engine has a `Level` control at the
top of its column, defaulting to 100 %. The convolution branch has its own peak
limiter, so `Src Feedback` and `IR Feedback` can be pushed without overloading
the mix:

- **Mix 0%** — ring modulation. The source is multiplied by the internal
  oscillator at `Ring Freq × Ring Ratio`.
- **Mix 100%** — vocoder. The carrier is split into a filter bank; each band's
  envelope drives the same band of the source.
- **Conv Mix 100%** — live convolution. Sliding FFT frames of 1/2 and 3/4 are
  multiplied and overlap-added. Feed a voice or an evolving texture on 3/4 to
  act as a live impulse response; silence there means no convolution output.

`Carrier = internal osc` makes the vocoder analyse its own oscillator, and
`Sync = ring → vocoder` feeds the ring-modulated signal back as its carrier.
With both, a source alone on 1/2 is enough to make those two engines sound.

## Controls

- **Ring Level / Vocoder Level / Conv Level** — per-engine trim, 0–100 %, at the
  top of each column; 100 % is unity, 0 % mutes just that engine
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
- **Conv Mix** — fades the ring/vocoder pair against live convolution, 0–100%;
  at the 0% default the plugin behaves exactly as before
- **Src Feedback / IR Feedback** — recirculate the convolution output one frame
  back into the source (1/2) or IR (3/4) frames; capped at 95 %, soft-limited in
  the loop, and followed by a peak limiter on the wet branch

## Randomise

Draws carrier frequency, ratio, shape, band count, spread, formant and tilt from
plausible territory, rounded so the values read as deliberate. `Mix`, `Conv Mix`,
the three Levels and `Bypass` are preserved so a sweep does not jump the balance.
Deterministic from the `Seed` parameter.

## MIDI CC

Five CC lanes drive Mix, Ring Freq, Ring Ratio, Bands and Carrier, on a
selectable MIDI channel. CC 1-4 are Drift's default lanes; CC 5 continues the
block. Setting a lane's CC number to 0 disables that override.

See `docs/design.md` for the DSP details and `profile.ttl` for the plugin
profile.
