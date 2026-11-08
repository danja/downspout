# Zmorph

Z-plane morph filter: morphs between two pole/zero sets (low-pass ↔ high-pass) with cutoff, resonance and morph controls.

## Controls
- Cutoff, Resonance
- Morph: 0 = low-pass, 50 = notch at the cutoff, 100 = high-pass
- Mix, Level

Magneto look. No latency. The two shapes share one denominator, so Morph 50 % cancels the
numerator at the cutoff (measured -109 dB there at mid resonance); the level through the
middle of the sweep sits about 6 dB down.