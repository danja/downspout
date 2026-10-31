# Interleaver

An unusual effect: two stereo inputs, **A** (channels 1/2) and **B** (3/4), are interleaved into one stereo output, either directly in the
time domain or with a phase vocoder. The aim is "interesting" more than "musical". See `docs/design.md` for how it works and the assumptions made.

| Control | Range | Default | Notes |
|---|---|---|---|
| Mode | Time, Spectral | Time | |
| Balance | 0-100 % | 50 | A's share of each cycle (Time) or of each bin period (Spectral); in Loudest it tilts the contest +-24 dB |
| Stereo Split | 0-100 % | 0 | Right channel's pattern is offset by this fraction of half a cycle; 100 % at Balance 50 is complementary |
| Output | -24 to +12 dB | 0 | |
| Slice | 0.02-200 ms (log) | 8 ms | Time: cycle = 2 x Slice (A segment + B segment at 50 %) |
| Sync | Free, Track A, Track B | Free | Lock the cycle to the pitch tracked on that input |
| Periods | 0.25-16 (log) | 1 | When tracking: cycle = 2 x Periods x pitch period |
| Crossfade | 0-100 % | 25 | Edge width as a share of the shorter segment |
| Edge Shape | Hard, Linear, Cosine | Cosine | Hard ignores Crossfade |
| Jitter | 0-100 % | 0 | Random cycle length, down to 0.25x |
| Scheme | Bins, Mag/Phase, Loudest | Bins | Spectral |
| Bin Width | 1-64 bins (log) | 4 | A bin period is 2 x width |
| Drift | -200 to +200 bins/s | 0 | Rotates the mask |
| Detected Pitch, Cycle Length | outputs | | Read-only status for the panel |

Latency is 2048 samples in both modes. Ordinary parameters only; no plugin state.
