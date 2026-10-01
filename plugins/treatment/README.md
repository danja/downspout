# Treatment

Treatment is a physically modelled acoustic treatment panel: the effect a
mass-air-mass absorber has on sound, derived from the panel's actual geometry
and fill rather than from a tone control.

A treatment panel is a facing sheet of some surface mass hung in front of a
wall, with a porous fill inside the cavity. The facing and the air behind it
form a mass-spring system, and the fill's flow resistance sets how sharply that
system absorbs at its own resonance. Get the flow resistance right and the panel
absorbs nearly everything that hits it at one frequency; get it wrong in either
direction and it does much less. That single fact drives the whole plugin.

Treatment therefore has an optimum, and it is not "more is better". Push the
fill too open or too dense and the panel gets worse, which is why Flow Resist
is the parameter worth understanding first.

## The model

All quantities are in real units: millimetres, kilograms per square metre, and
Rayl/m. There are no hidden scale factors.

With `m` the facing surface mass, `D = cavity + gap` the compliant depth, `c`
the speed of sound and `rho0` the density of air:

- **Resonance** `w0 = c * sqrt(rho0 / (m * D))` — the standard mass-air-mass
  frequency. The facing and the air gap are in series acoustically, so they add.
- **Specific flow resistance** `r = flowResist * cavity` — the resistance the
  fill presents per unit area.
- **Surface impedance** `Z = r + j(w*m - rho0*c^2/(w*D))`
- **Absorption** `alpha = 4*rho0*c*r / (|Z + rho0*c|^2)`, which peaks exactly at
  `w0`, where the imaginary part of Z vanishes.
- **Impedance match** `4*r*rho0*c / (r + rho0*c)^2`, maximal at `r = rho0*c`.
  This is why the panel has a sweet spot rather than a monotonic response.
- **Fill diffusion corner** `R / (2*pi*rho0*cavity)`, above which the fill keeps
  absorbing across the mids and top.

The audio path is the complement of the absorption: what the panel does not
absorb gets through. Two biquads realise it — a resonant dip at `w0` carrying
the model's own Q, and a high shelf at the diffusion corner for the fill's
broadband contribution. `Amount` scales `alpha` itself rather than
crossfading, so Amount 0 is a bit-transparent filter instead of a mix, and the
response stays monotonic as you sweep it.

Because both stages are cuts and nothing in the chain boosts, the plugin can
never make a signal louder. That is a property the tests assert directly.

## Parameters

| Parameter   | Range        | Default | Notes                                       |
|-------------|--------------|---------|---------------------------------------------|
| Cavity      | 20–400 mm    | 100 mm  | Depth of the absorptive fill; CC 2          |
| Air Gap     | 0–400 mm     | 50 mm   | Gap to the wall; CC 3                       |
| Facing Mass | 0.2–8 kg/m²  | 0.8     | Facing sheet density; CC 4                  |
| Flow Resist | 1–60 kRayl/m | 8 kRayl/m | Fill resistivity; CC 5. Sweet spot near 4.1 |
| Amount      | 0–100 %      | 100 %   | How much absorption; CC 1                   |
| Bypass      | off/on       | off     | DSP keeps running, so toggling is click-free |
| Seed        | 1–9999       | 1       | Drives Randomise                            |
| Randomise   | trigger      | —       | Builds a new plausible panel                |

Cavity and Air Gap are in millimetres because that is how panels are specified.
Facing Mass in kg/m² is how facing sheets are specified. Flow Resist in Rayl/m
is how fills are specified. The panel shows the derived total depth, resonance,
Q, peak absorption and diffusion corner so you can see what the numbers mean.

## Randomise

Randomise draws a new cavity, gap, facing mass and flow resistivity from the
seed, rounded to values a person would actually pick. It advances the seed, so a
second press gives a different panel, and it is reproducible: the same seed
always produces the same panel.

Amount and Bypass are deliberately left alone, so randomising the panel in the
middle of a performance does not change how much treatment you have or punch a
hole in the signal.

## Drift CC routing

Route Drift's MIDI output to Treatment's MIDI input and all five lanes land
directly:

| CC | Parameter  |
|----|------------|
| 1  | Amount     |
| 2  | Cavity     |
| 3  | Air Gap    |
| 4  | Facing Mass|
| 5  | Flow Resist|

CC 1–4 are Drift's default lanes; CC 5 continues the block. Each control's CC
number is a dropdown in the panel, and setting one to `off` disables the
override for that control. CC Channel selects which MIDI channel is listened
to.

This is the same convention `magneto`, `helterskelter`, `chipper`, `ghost` and
`spliff` use.

## Notes

- Changing geometry moves the resonance live, and the readouts follow.
- The DSP is two biquads per channel with smoothed coefficients, so parameter
  changes never click.
- Non-finite input samples are contained and cannot poison the output bus.
- Host validation in REAPER is still pending; see `docs/design.md`.
