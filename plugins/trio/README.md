# Trio

Three-band dynamics: each band compresses downward above a threshold and lifts quiet signals upward
below another, with adjustable crossovers. A redesign inspired by the Multiband Dynamics in
`~/github/oxide-studio` (`oxide-fx`, MIT/Apache-2.0), not a port: the source shares one envelope between
the left and right channels of a band and updates it twice per sample (see `docs/design.md`).

The panel plots the three bands with draggable crossover lines and a live bar for the gain each band is
applying (reduction down, lift up). No MIDI, no transport, no latency.

| Control | Range | Default |
|---|---|---|
| Low / high crossover | 40-1000 Hz / 1-12 kHz (logarithmic; high is held at least 1.25 x low) | 250 Hz / 2.5 kHz |
| Input | -12 to +24 dB (into the bands; the dry path is untouched) | 0 dB |
| Output | -24 to +12 dB (wet signal) | 0 dB |
| Amount | 0-100 % wet | 100 % |
| Time | 0.1-10 x (scales every attack and release) | 1 |
| Upward limit | 0-24 dB | 18 dB |
| Per band: On | Off / On (off = the band passes through) | On |
| Per band: Down threshold / ratio | -60 to 0 dB / 1-20 : 1 | low -24 dB, mid -20, high -18 / 4 : 1 |
| Per band: Up threshold / ratio | -80 to 0 dB / 1-10 : 1 (1 = no lift) | low -36 dB, mid -32, high -30 / 2.5 : 1 |
| Per band: Attack / release | 0.5-200 ms / 5-2000 ms (logarithmic) | low 10 / 100 ms, mid 5 / 80, high 2 / 50 |
| Per band: Gain | -18 to +18 dB | 0 dB |

The defaults are the canonical "OTT" settings, so a fresh instance is a strong effect: lower Amount to dial
it back, or set every ratio to 1 for a transparent three-band split (the crossover sums flat). The louder
channel of a band sets the reduction for both. The upward lift is held to the down threshold so the two
cannot fight. Settings are ordinary automatable parameters; there is no plugin state. Six output parameters
(`status_gain_1-3`, `status_level_1-3`) feed the panel.
