# Grist

Granular grain-delay effect. The stereo input is recorded into a seven-second history and
replayed by windowed grains, one per **Window** / **Density**. Each grain draws its own start
(**Blur**), direction (**Reverse**), stereo position (**Spread**), length (**Variance**) and
pitch (**Pitch** plus **Range**, optionally on a **Scale**). **Window** and the feedback
**Delay** can follow the host tempo. **Feedback** returns the grains through a delay and an
optional filter. See `docs/design.md`.

No MIDI. Adds latency of about Blur x Window / 2 plus (ratio - 1) x Window when pitched up,
shown on the panel and not reported to the host.

| Control | Range | Default |
|---|---|---|
| Dry/wet | 0-100 % | 50 % |
| Window | 1-1000 ms, or 1/64 to 8/1 when synced | 50 ms |
| Pitch | -24 to +24 st | 0 |
| Density | 1-4 grains per window | 2 |
| Blur | 0-2 windows | 0.5 |
| Reverse | 0-100 % | 0 |
| Spread | 0-100 % | 30 % |
| Shape | Hann ... percussive | Hann |
| Range / Direction / Scale | 0-24 st, Up/Down/Both, 7 scales | 0 st, Both, Semitones |
| Variance | 0-100 % | 0 |
| Feedback | 0-0.95 | 0 |
| Delay | 1-1000 ms, or synced | 100 ms |
| Feedback filter | Off/On, Lowpass/Highpass/Bandpass, frequency, width | Off |
| Seed | 1-65535 | 1 |

Settings are ordinary parameters; the history is not saved.

## Build

Part of the root build (`DOWNSPOUT_BUILD_GRIST`, default ON):

```
cmake --build build --target grist-vst3
ctest --test-dir build -R grist
```

Run `env -u DISPLAY scripts/capture-plugin-screenshots.sh grist` for the catalogue image.
