---
title: NAM
order: 225
bundle: nam.vst3
kind: Audio effect
category: processor
role: Neural amp and cabinet
screenshot: /assets/plugins/nam.png
capabilities: [audio input, audio output]
summary: A Neural Amp Modeler player. Load a .nam amp capture and a .wav cabinet impulse response, shape them with gain, gate, tone and cabinet cuts, and mix them back with the dry signal.
---

## Opinion

Not yet auditioned in a host with real captures. Downspout had distortions (Damiano, Skream, Chipper) but nothing that
plays the neural captures of real amps and pedals that people share as `.nam` files. This is a player for those, with the
strip around it that a guitar insert needs: a gate before the model, a tone control after it, and a cabinet. It is also useful on
bass, synths and drum buses, where a capture acts as a distortion that responds to level the way hardware does.

## Functionality

The model runs in Steven Atkinson's NeuralAmpModelerCore, so a given `.nam` file behaves as it does in other NAM hosts. The cabinet is
Downspout's own convolver: the first 128 taps run sample by sample and the rest in the frequency domain, so it adds no delay.

`input -> Input gain -> Noise gate -> model -> Loudness -> Bass / Mid / Treble -> cabinet -> Low / High cut -> IR gain -> Dry/wet -> Output gain`

### Files

| Slot | Accepts | Notes |
|---|---|---|
| Model | `.nam`, one input and one output channel | WaveNet (A1, A2), LSTM, ConvNet, Linear and slimmable containers. A model trained at another sample rate is resampled around the network and the latency is reported to the host |
| Cabinet | `.wav`, mono or stereo, 16/24/32-bit PCM or float | Resampled to the host rate and trimmed to 2 s. A mono IR is used on both sides; a stereo IR uses its left and right |

Only the file paths are saved with the project, so keep the files where the project can find them. A file that cannot be read turns the filename red in its panel.

### Parameters

| Control | Range | Default | Notes |
|---|---|---|---|
| Input gain | -24 to +24 dB | 0 dB | Into the gate and the model: the drive control for most captures |
| Noise gate | -90 to -20 dB | Off | Before the model |
| Bass, Mid, Treble | -12 to +12 dB | 0 dB | 120 Hz shelf, 750 Hz peak, 3.5 kHz shelf |
| Low cut, High cut | 20-500 Hz, 2-20 kHz | Off, Off | After the cabinet |
| IR gain | -24 to +12 dB | 0 dB | IRs are not normalised |
| Output gain | -24 to +24 dB | 0 dB | Processed signal only |
| Dry / wet | 0-100 % | 100 % | The dry signal is delayed to match |
| Channels | Mono sum / Stereo | Mono sum | Stereo runs one model per side, twice the CPU |
| Model, Cabinet | Bypass / Active | Active | |
| Loudness | Raw / -18 dB | -18 dB | Needs a model that reports its loudness |

### Cost

A standard WaveNet capture takes about 37 % of one core per network on the test machine; the A2 example about 11 %. Stereo mode doubles it.

### Status

Core tests cover the convolver (against direct computation), the resampler, the tone curve, the gate, all nine of the NAM core's example models, the resampled path, IR loading, and that every control changes the sound.
The real DPF wrapper was driven through restore, reactivation, a sample-rate change and bad files with a manual probe. Not yet auditioned in a host with real captures.
