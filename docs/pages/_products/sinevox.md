---
title: Sinevox
order: 215
bundle: sinevox.vst3
kind: Audio effect
category: processor
role: Formant vocoder and sine-wave-speech generator
screenshot: /assets/plugins/sinevox.png
capabilities: [audio input and output, MIDI CC and note input, MIDI CC and note output]
summary: A formant tracker with two voices — a vocoder whose band-pass bank follows the speech's formants, and a sine-wave-speech generator — with MIDI control and formant-to-MIDI output.
---

## Opinion

Most vocoders split the spectrum into a fixed set of bands and follow the level in
each. Sinevox does something different: it finds the handful of peaks that make a
vowel a vowel — the formants — and follows those. Four to six moving band-pass
filters, or four to six moving sine waves, are enough to keep speech intelligible.
That is what sine-wave speech is, and it is startling the first time: three
whistles that obviously say a word.

The two modes are the same analysis spoken two ways. Sine Speech needs nothing but
a voice on inputs 1/2. Vocoder wants a carrier with harmonics on 3/4 — a saw pad, a
detuned pulse, a choir — and gives you a robot whose vowels are carved by far fewer
filters than a classic channel vocoder, which makes it thinner and stranger and
easier to play with. Shift moves every formant at once, so you can change the size
of the vocal tract without touching the pitch of the carrier.

The part nobody asked for but that turned out to be the most fun: MIDI Out. Send the
formants as CCs and a spoken vowel becomes four control lines for a filter or an
FM index. Send them as notes and speech becomes a stream of chords.

## Functionality

Speech on 1/2 is summed to mono, decimated to about 16 kHz and analysed every 5 ms
with a linear-predictive model of order 2N+2. The poles give each formant a centre
frequency and a bandwidth (so a Q); the envelope at the peak gives its amplitude.
Formants hold their identity between frames, so they glide instead of swapping.

The plot at the top of the panel is the analyser's view, live: a bell per formant
and the power-sum envelope. Under it are the numbers. Triangles on the axis show
where Shift will put each formant.

### Parameters

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Mode | vocoder / sine speech / both | vocoder | CC 5 |
| Formants | 1–8 | 4 | CC 3 |
| Gate | −80…−20 dB | −55 | Tracker silent below |
| BW Limit | 100–3000 Hz | 700 | Wider resonances are not formants |
| Glide | 1–200 ms | 25 | Frequency / Q smoothing |
| Attack, Release | 1–100, 5–500 ms | 6, 70 | Amplitude follower |
| Shift | ±12 st | 0 | CC 1, pad X |
| Q Scale | ×0.25–×4 | ×1 | CC 2, pad Y |
| Tilt | ±12 dB/oct | 0 | CC 4 |
| Level | ±24 dB | 0 | |
| Speech Mix | 0–100 % | 0 | Dry speech |
| Carrier Norm | 0–100 % | 100 | Vocoder only |
| Sine Spread | 0–100 % | 30 | Sine modes only |
| Fundamental | 0–100 % | 0 | Sine at the tracked pitch, every mode |
| Note Shift | off / on | off | Held note shifts from middle C |
| MIDI Out | off / CCs / notes | off | CC 20–27 frequency, CC 102–109 level |
| Out / In Channel | 1–16 | 1 | |
| Bypass | off / on | off | Speech passes through |

### Limits

Tracks follow the spectral envelope, so very high voices can lock onto harmonics,
and fricatives give unsteady wide tracks. CC changes reach the processor but the
panel does not follow them.

Core DSP, deterministic tests, the VST3 target, the panel and the catalog screenshot
are complete. Host validation in REAPER is pending.
