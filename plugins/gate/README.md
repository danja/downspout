# Gate

Gate is a standalone, linked stereo noise gate with an optional stereo
sidechain. One detector and one gain envelope drive both main channels, so the
stereo image is not modulated independently. The sidechain can listen to the
main inputs or to audio inputs 3/4; if External is selected and nothing is
connected, the detector sees silence and the gate stays closed.

```
main L/R      -> gated L/R (shared gain)
sidechain L/R -> optional HPF -> RMS detector -> threshold/hysteresis
                                              -> hold -> attack/release
```

| Control | Range | Default | Notes |
|---|---|---|---|
| Threshold | -80 to 0 dB | -40 dB | Opens above this detector level |
| Hysteresis | 0 to 20 dB | 6 dB | Closes only below `Threshold - Hysteresis` |
| Range | 0 to 80 dB | 60 dB | Maximum closed attenuation; 0 dB is transparent |
| Attack | 0 to 100 ms | 5 ms | Opening time of the shared gain |
| Hold | 0 to 500 ms | 50 ms | Counts only while the detector is below the close point |
| Release | 10 to 2000 ms | 200 ms | Closing time of the shared gain |
| SC Source | Internal / External | Internal | External uses audio inputs 3/4 |
| SC HPF | 20 to 500 Hz | 20 Hz | Detector only; 20 Hz is exactly off |
| Output | -12 to 12 dB | 0 dB | Post-gate trim |
| Bypass | Off / On | Off | 5 ms crossfade to the untouched main input |

The detector is the mean square of the sidechain pair with a 2 ms rise and 40 ms
fall, read in dB. The gate opens when the level crosses Threshold and closes
only after it falls below the close point for Hold plus Release. The applied
gain is `10^(-Range x (1 - open) / 20)`. All state is double precision; tiny
values are flushed to avoid denormals and non-finite input samples are treated
as silence.

The **Detector**, **Reduction** and **Gate** outputs are read-only meters for the
panel. Main audio is 2 in/2 out with a second stereo input pair for the
sidechain; there is no MIDI, transport, latency or plugin state. All ten writable
settings are ordinary automatable parameters.

See `docs/design.md` for the signal-path decisions and test coverage.
