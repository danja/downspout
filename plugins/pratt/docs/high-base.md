# What a much higher Pratt base does

Question: the synth uses `n = (note + 1) x base` with base limited to 64 (so `n <= 8192`). The
original paper says an integer names a filter, its prime factors name reusable modules, and
multiplying the labels cascades the modules. What happens if the base is allowed to be much larger?

Reproduce with `tools/pratt_base_probe.py` (numpy only; pass a prime limit, default 100000).
The C++ checks below were run on a scratch copy of `pratt_poly` with `kMaxIndex` raised to 2^20;
the shipped code is unchanged.

## It stays valid far beyond 8192

| Quantity | n <= 8192 (today) | n up to 1,048,576 |
|---|---|---|
| Max polynomial degree | 13 | 20 (at n = 2^20, `f_n = x^20`) |
| Max coefficient | 26 (primes) | 924 (n = 3^12) |
| Worst `Re(t - 2)` (stable if negative) | -0.9977 at n = 6173 | -0.9779 sampled, -0.9751 over every prime to 1,048,576 |
| Root-finder residual `max |f_n(t)|/n` | 2.8e-17 | 2.8e-17 |

- int64 coefficients stay exact, the Durand-Kerner plus Newton roots stay accurate, and every
  filter stays strictly stable. The margin shrinks slowly and settles near -0.975: it does not
  head towards instability.
- Beyond that, the spectral table above reached degree 21 at n of about 4 million (base 65,537) and 23 at about 16 million.

## It does not give more colour; it gives a darker, smoother voice

Multiplication adds attenuation in dB (paper section 5.1), so a large base appends many extra
low-pass modules to every note. Note 60, xi 0.16, roll 1.0:

| base | n | degree | harmonic 4 | harmonic 16 | spectral centroid |
|---|---|---|---|---|---|
| 1 | 61 | 5 | -13.3 dB | -41 dB | 3.55 |
| 16 | 976 | 9 | -14.9 | -58 | 2.62 |
| 64 | 3,904 | 11 | -15.7 | -66 | 2.40 |
| 1,009 | 61,549 | 13 | -15.6 | -68 | 2.39 |
| 12,347 | 753,167 | 18 | -18.2 | -94 | 2.00 |
| 65,537 | 3,997,757 | 21 | -19.6 | -108 | 1.87 |

- No resonant peak appears: harmonic 1 stays the loudest. Everything becomes closer to a sine.
- It is not monotonic in the base. Smooth composites such as 1024 (`f_2^10`) are darker than a
  nearby prime (1009).

The key-to-key irregularity that gives the instrument its character comes from `f_(note+1)`, which
changes factorisation from key to key. A large base multiplies in the same `f_base` for every key,
so that variation is swamped. With brightness re-scaled so the median centroid is equal:

| base | key-to-key centroid std | mean jump between neighbouring keys |
|---|---|---|
| 1 | 0.38 | 0.41 |
| 16 | 0.17 | 0.19 |
| 64 | 0.13 | 0.15 |
| 1,009 | 0.13 | 0.15 |
| 65,537 | 0.06 | 0.07 |

The original's presets use small primes (2, 3, 5, 7, 11, 17, 37). Its darkest voice, Pad, has the
largest, 37, which fits this.

## If the limit were raised

- `kMaxIndex` (8192) and the `base` range (0-64) would need raising, and `kMaxSections` (13) would
  need to become at least 20 for n up to 2^20 (2^22 alone has degree 22); size it from the chosen
  maximum with a test over every n.
- `Engine::warmCaches()` precomputes roots for every index up to `kMaxIndex`. At 2^20 indices that
  is about 5 s of CPU and a large table, so it would have to warm only the indices a voice uses.
- The 20 ms crossfade and Filter mode work unchanged: the cascade is just more biquads.
- Brightness would have to compensate: roughly 2.7 times higher at base 65,537 to match base 1.

## Possible uses

- A "Smooth" or "Pure" voice from a large base, as the Pad preset already does in miniature.
- Following the paper's note that pitch can be set independently of the arithmetic timbre: a
  separate timbre index that does not multiply `note + 1`, so the character stays tied to key
  factorisation while a fixed module chain adds the roll-off.

## Implemented: Timbre Index

The separate timbre index is now a parameter (`timbre`, 1-8192, default 1). It cascades `H_timbre`
onto every note's own `H_(note+1)*base` rather than multiplying into the base, so it keeps the
key-to-key character of the factorisation. At 1 it is skipped entirely. The synth evaluates it
while building a wavetable, never per sample, and a table costs about 0.8 ms to build at any
setting, so there is nothing for the default to save and nothing extra to pay when it is raised.
The raised-limit options above (bigger base range, `kMaxSections`) are still unimplemented.
