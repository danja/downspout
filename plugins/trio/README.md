# Trio

Three-band dynamics: each band compresses downward above a threshold and lifts quiet signals upward
below another, with adjustable crossovers. A redesign inspired by the Multiband Dynamics in
`~/github/oxide-studio` (`oxide-fx`, MIT/Apache-2.0), not a port: the source shares one envelope between
the left and right channels of a band and updates it twice per sample (see `docs/design.md`).

The panel plots the three bands with draggable crossover lines and a live bar for the gain each band is
applying (reduction down, lift up). MIDI in for Drift (below); no transport, no latency.

| Control | Range | Default |
|---|---|---|
| Low / high crossover | 40-1000 Hz / 1-12 kHz (logarithmic; high is held at least 1.25 x low) | 250 Hz / 2.5 kHz |
| Input | -12 to +24 dB (into the bands; the dry path is untouched) | 0 dB |
| Output | -24 to +12 dB (wet signal) | 0 dB |
| Amount | 0-100 % wet | 100 % |
| Time | 0.1-10 x (scales every attack and release) | 1 |
| Upward limit | 0-24 dB | 18 dB |
| Per band: On | Off / On (off = the band passes through) | On |
| Per band: Down threshold / ratio | -60 to 0 dB / 1-20 : 1 | low -16 dB, mid -14, high -12 / 2 : 1 |
| Per band: Up threshold / ratio | -80 to 0 dB / 0.2-10 : 1 (1 = no lift, above 1 lifts, below 1 expands downward: 0.5 = 2:1 expander) | low -40 dB, mid -38, high -36 / 1.5 : 1 |
| Per band: Attack / release | 0.5-200 ms / 5-2000 ms (logarithmic) | low 10 / 100 ms, mid 5 / 80, high 2 / 50 |
| Per band: Gain | -18 to +18 dB | 0 dB |
| CC channel | 0-16 | 1 (0 ignores MIDI): CC 1-4 drive Amount, Time (logarithmic), Input and Upward limit until you move that control or automate it |
| Knee | 0-24 dB | 0 (hard) | Quadratic soft knee centred on both thresholds |
| Solo | Off, Low, Mid, High | Off | Only that band's processed signal is heard; Amount and the dry path are ignored |
| Detectors listen to | Input, Sidechain | Input | Sidechain is inputs 3-4, split into the same three bands; with nothing connected it falls back to the input |

The defaults are gentle (2:1 from -16/-14/-12 dB, a 1.5:1 lift below -40/-38/-36 dB). For the strong "OTT" sound use 4:1 and 2.5:1; to dial
it back, or set every ratio to 1 for a transparent three-band split (the crossover sums flat). The louder
channel of a band sets the reduction for both. The upward lift is held to the down threshold so the two
cannot fight. Settings are ordinary automatable parameters; there is no plugin state. Six output parameters
(`status_gain_1-3`, `status_level_1-3`) feed the panel.
