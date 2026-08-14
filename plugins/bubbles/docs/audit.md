# Bubbles — LV2 source audit

Source: `/home/danny/github/flues/lv2/bubbles`

## Parameters in the LV2 version

All normalized to 0–1 except Mode (0–4 int).

| Port | Name        | Default | Notes |
|------|-------------|---------|-------|
| 3    | Intensity   | 0.55    | Output envelope amplitude scaling |
| 4    | Density     | 0.50    | Base bubble/drip spawn rate |
| 5    | Size        | 0.45    | Bubble resonance frequency (blended with MIDI pitch) |
| 6    | Flow Rate   | 0.50    | Turbulence coupling |
| 7    | Brightness  | 0.55    | LP filter cutoff scaling |
| 8    | Resonance   | 0.55    | Resonator Q |
| 9    | Depth       | 0.20    | Underwater LP mix |
| 10   | Space       | 0.35    | Stereo delay wet/feedback |
| 11   | Randomness  | 0.40    | Frequency jitter |
| 12   | Heat        | 0.30    | Boiling rate multiplier |
| 13   | Output      | 0.80    | Post-saturation gain |
| 14   | Mode        | 4       | 0=Flow, 1=Bubble, 2=Drip, 3=Underwater, 4=Hybrid |
| 15   | Noise Floor | 0.12    | Background noise level |
| 16   | Drive       | 0.35    | Saturation |

## LV2 DSP approach

1. LCG white noise (`0x12345678 × 1664525 + 1013904223`)
2. Two `OnePole` LP filters → bandpass by subtraction for flow texture
3. Two `BiquadBandPass` turbulence bands
4. 36 bubble voices: impulse-decay + bandpass resonator + envelope
5. 16 drip voices: dual bandpass resonator + envelope
6. 4 modal body resonances (fixed weights 0.18 / 0.13 / 0.09 / 0.06)
7. DC blocker (`r = 0.9985`)
8. Tanh saturation (pre + post drive)
9. Cross-coupled stereo delay (~42 ms / ~64 ms)
10. MIDI: note-on sets gate + velocity + MIDI-based size
11. Filter update every 64 samples

## Modes (LV2)

| Mode       | Character |
|------------|-----------|
| Flow       | Dominant noise flow |
| Bubble     | Dominant bubble events |
| Drip       | Dominant drip events |
| Underwater | Deep LP, body resonance emphasis |
| Hybrid     | Balanced all layers |

## Deviations in the Downspout port

- Mode set expanded: Stream, River, Ocean, Bubbles, Drips, Rain, Custom (7 modes)
- `Intensity` and `Noise Floor` merged into `Flow` and a fixed internal noise floor
- `Flow Rate` renamed `Turbulence` with clearer semantic (noise band roughness)
- Depth LP now applied per-channel as a separate one-pole filter (not just mix)
- Wave oscillator added for ocean/river amplitude modulation
- Plugin runs continuously without MIDI (gateAmp initialised to 1.0)
- No `DISTRHO_PLUGIN_WANT_TIMEPOS` (Bubbles is not transport-locked)
