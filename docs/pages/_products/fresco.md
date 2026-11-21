---
title: Fresco
order: 241
bundle: fresco.vst3
kind: Audio effect
category: processor
group: spectral
role: Spectrogram painter
screenshot: /assets/plugins/fresco.png
capabilities: [audio input, audio output, transport]
summary: "Paints a picture (PNG, PGM/PPM or BMP) into the audio's spectrogram: height is frequency, brightness is level, and a free-running or host-locked playhead sweeps the picture."
---

## Opinion

Verified in a REAPER offline render. Put a spectrum analyser or spectrogram after it, feed it white noise or a pad, press play and the built-in rings and cross should draw themselves;
then **Load picture** and try a high-contrast drawing or a logo with large shapes. Source **Tone** is the clean way to see a picture (steady sinusoids); **Input** keeps the input's own
timing and pitch and is the way to make a voice or a synth "wear" the picture. At the default **Follow input level** of 0 % the picture drones even with no audio input; raise it to
100 % for silence in, silence out. Bright, crowded pictures are loud: trim with **Output**.

## Functionality

A 2048-point short-time Fourier transform (Hann, hop 512) whose magnitudes are replaced by the picture's column under the playhead. **Bottom**/**Top** set the frequencies at the
picture's edges (**Axis** Log, as most spectrograms are, or Linear); **Range**, **Contrast** and **Pixels** set how brightness becomes level (black is silence).
**Length** is the time the playhead takes to cross the picture, as a free loop or locked to the host timeline. **Follow input level** can make silence in silent out. Latency 2048 samples,
reported to the host. The picture's path is saved with the project. JPEG, GIF, WebP and video are not supported (the plugin says so when you load one).

### Status

Core covered by tests (decoders against PNGs written by Python's zlib in nine formats and every row filter, damaged files under ASan, band position and level against formulas, playhead,
transport, block-size independence), mutation-checked, and verified by a REAPER offline render. Not seen on a real analyser.
