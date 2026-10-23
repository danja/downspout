# Glue

Stereo-linked bus compressor. A redesign inspired by the Glue Compressor in
`~/github/oxide-studio` (`oxide-fx`, MIT/Apache-2.0), not a port: the source is a plain
hard-knee peak compressor and several of its README claims do not match its code (see
`docs/design.md`).

The panel shows the static transfer curve (from the same function the tests hold the audio to),
a live dot for where the signal is now, and a scrolling gain-reduction history. Gain reduction and
detector level are measured from the audio. No MIDI, no transport, no latency (no look-ahead).

| Control | Range | Default | Notes |
|---|---|---|---|
| Threshold | -60 to 0 dB | -18 dB | On the detector level |
| Ratio | 1.1 to 20 : 1 (logarithmic) | 4 : 1 | |
| Knee | 0 to 24 dB | 6 dB | Soft knee centred on the threshold |
| Range | 1 to 40 dB | 40 dB | Caps the gain reduction |
| Attack | 0.1 to 100 ms (logarithmic) | 10 ms | |
| Release | 20 to 2000 ms (logarithmic) | 200 ms | Ignored while Release mode is Auto |
| Release mode | Fixed / Auto | Fixed | Auto: about 40 ms after a transient, up to 800 ms once the compression has been sustained |
| Sidechain HPF | 20 to 400 Hz (logarithmic); 20 = off | 60 Hz | Second-order high-pass on the detector only |
| Detector | Peak / RMS | Peak | RMS uses a 10 ms window |
| Makeup | 0 to 24 dB | 0 dB | |
| Soft clip | Off / On | Off | Linear below 0.5, tanh shoulder above, never exceeds 1.0 |
| Mix | 0 to 100 % | 100 % | Parallel compression |

The louder channel sets the reduction for both, so the stereo image does not move.
Settings are ordinary automatable parameters, saved by the host; there is no plugin state.
Two output parameters (`status_reduction`, `status_level`) feed the panel.
