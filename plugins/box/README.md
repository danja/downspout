# Box

Box is a stereo audio effect that models the *wooden structure* of an acoustic
instrument: a guitar top and box, a piano soundboard and rim, a loudspeaker-like
cabinet, or a free plate. It does not model strings. The input excites the wood
and the output is what the wood radiates, so a bare signal gains the colouration
and decay of a resonator; a percussive or harmonic source can be made to sound
as if it is being played through a real body.

## Model

The plate is rectangular and simply supported. For mode `(m, n)`:

```
D = E h³ / (12 (1 - ν²))          ν = 0.3
f_mn = (π/2) sqrt(D / (ρ h)) (m²/a² + n²/b²)
```

`a` and `b` come from **Body** and **Body Size**, `h` from **Thickness**, `E`
from **Stiffness** and `ρ` from **Density**. Twelve plate modes are rendered as
resonant band-pass filters with a Q derived from **Damping** and the mode's T60.
The **Excitation** and **Pickup** XY pads weight each mode by
`sin(mπx)·sin(nπy)` at the drive and read points, so moving either pad changes
the timbre rather than just the level.

A guitar or cabinet also has enclosed air. An open **Soundhole** adds a
Helmholtz resonance

```
f = (c / 2π) sqrt(A / (V (t + 1.7 r)))
```

and **Air Volume** and **Body Depth** set the two axial cavity modes.
**Piano** and **Free Plate** deliberately remove this air path.

**Drive** soft-clips the excitation and, with **Wood Nonlinearity**, stiffens the
panel as level rises. Both are exaggerated effect controls, not calibrated
material behaviour. Physical parameters glide over 12 ms and a fresh, stable
biquad is designed every 32 samples; only gains are smoothed per sample. This
keeps automation and MIDI moves click-free without interpolating filter
coefficients between two designs, which could destabilise a Size sweep.

## Parameters

| Group | Parameters | Range / defaults |
|-------|------------|------------------|
| Structure | Body, Body Size, Thickness, Stiffness, Density, Damping | Guitar / 1x / 3 mm / 11 GPa / 450 kg/m³ / 1x |
| Air | Soundhole, Air Volume, Body Depth | 90 mm / 15 L / 100 mm |
| Play | Drive, Wood Nonlinearity, Excitation X/Y, Pickup X/Y, Stereo Width | 6 dB / 15% / (50, 35)% / (50, 50)% / 30% |
| Integration | Dry/Wood Mix, Output, Bypass, Seed, Randomise, CC Channel | 35% / 0 dB / off / 1 / button / Ch 1 |
| Read-only | Randomise Count, Body Mode, Air Mode, Decay, Mode Q | derived from the current structure |

Every property has a deliberately wide range. Size spans 0.05x–4x (frequency
falls with the square of size), thickness 0.5–40 mm, stiffness 0.5–40 GPa,
density 100–1500 kg/m³ and damping 0.02–8x. The readouts and the response plot
show what those numbers mean.

## MIDI

Route Drift (or any controller) to Box's MIDI input. CC 1–4 overwrite the
corresponding host parameters on the selected CC Channel:

| CC | Parameter |
|----|-----------|
| 1  | Body Size |
| 2  | Damping |
| 3  | Soundhole |
| 4  | Drive |

The mapping is fixed; there are no per-CC dropdowns. Values are applied to the
full published range using the same logarithmic track as the UI sliders, so a
CC at 64 means the same thing as a slider at 50%.

## State and lifecycle

All settings are ordinary automatable parameters. Box declares no extra host
state, so DPF saves no default snapshot into every project. `activate()` and a
sample-rate change clear only the filter histories and envelope; the patch is
never reset. Randomise is a UI action, not a host trigger: it derives a new
plausible body from Seed, commits the result as ordinary parameter values,
advances the seed, and leaves Mix, Output, Bypass and CC Channel alone. Its
read-only count reports how many draws this panel has made.
