---
title: Voxmod
order: 206
bundle: voxmod.vst3
kind: Audio effect
category: processor
group: spectral
role: Vocoder, ring modulator and live convolution
screenshot: /assets/plugins/voxmod.png
capabilities: [audio input and output, MIDI CC input]
summary: Three parallel engines — a filter-bank vocoder, a band-limited ring modulator and sliding-window FFT convolution cross-synthesis with source and IR feedback.
---

## Opinion

Voxmod is one plugin with three engines that share a bus rather than a mode
switch, which is the whole point of it. Put an instrument on inputs 1/2 and a
voice on 3/4, and start at Mix 50 with Conv Mix at 0: you get the instrument
coloured by the voice's formants with the ring modulator underneath filling the
gaps. Push Mix left and the ring mod takes over; push it right and the voice
takes over. Raise Conv Mix and the two fade down as sliding FFT frames of the
two inputs are multiplied — the voice on 3/4 becomes a live impulse response for
the instrument on 1/2. The x-y pad is X for Mix and Y for ring frequency; each
engine's column starts with its own Level, and the convolution column then owns
Conv Mix and its two feedback taps.

It wants a modulator with harmonics. A clean synth pad through the vocoder sounds
like a filtered version of itself, because there is nothing in it for the carrier's
envelope to grab. A drum bus, a distorted guitar, a bowed string, anything with
partials, comes alive. Convolution is different: it needs energy on *both*
pairs, and silence on 3/4 means no wet output at all.

Carrier is set to `input 3/4` by default, which is the correct default for a
two-source setup. If you have only one thing, switch Carrier to `internal osc`
and the vocoder analyses its own oscillator, or turn Sync on so the ring-modulated
signal feeds back as its carrier — either way inputs 1/2 alone are enough for
those two engines.

## Functionality

Three engines, two blend parameters.

The **ring modulator** multiplies the modulator by a band-limited oscillator at
`Ring Freq × Ring Ratio`. Ring Ratio is quantised to 16 musical steps from −12 to
+35 semitones, so moving it changes the sidebands in semitones and lands on the
same ratios every time, rather than sweeping continuously through a four-octave
range. Ring Depth scales the oscillator toward unity, so 0 % is a clean
pass-through and 100 % is full multiplication. The saw and square shapes are
polyBLEP-corrected: the oscillator's job is to produce only the carrier, and
unwanted aliasing would fold broadband noise across the modulator and drown out
the discrete sidebands that make a ring modulator sound like one.

The **vocoder** splits the carrier into a filter bank and follows each band's
envelope with an asymmetric follower, then applies those gains to the matching
bands of the modulator. Band edges are logarithmic across `Spread` octaves
because a linear bank would spend most of its bands on treble and starve the
200–1200 Hz region that actually carries vowel identity. Bands are constant-skirt
rather than constant-Q, so the resynthesis gain does not tilt with band width.

Formant transposes the modulator side of the bank only. Analysing the carrier at
one frequency and resynthesising the modulator at a transposed one is what
relocates the envelope that shapes the output — moving both sides together would
just transpose everything and change nothing about the vowel.

The **convolver** has no stored impulse response. Every 256 samples it windows
1024 samples from each input pair, multiplies their 2048-point FFT frames, and
overlap-adds the inverse transform. Re-analysing the IR feed on every hop is what
makes it live: a moving voice or texture on 3/4 becomes a moving filter on 1/2.
`Src Feedback` and `IR Feedback` recirculate the output one frame back into
either frame buffer; both are capped at 95 %, soft-limited inside the loop and
default to off. The frame product is normalised by both transform lengths, and a
peak limiter on the wet branch stops feedback from pushing the track into
overload without compressing the ring or vocoder paths.

### Parameters

| Parameter      | Range        | Default | Notes                                        |
|----------------|--------------|---------|----------------------------------------------|
| Ring Level     | 0–100 %      | 100 %   | Top of the ring column; unity by default     |
| Vocoder Level  | 0–100 %      | 100 %   | Top of the vocoder column                    |
| Conv Level     | 0–100 %      | 100 %   | Top of the convolution column                |
| Mix            | 0–100 %      | 50 %    | Vocoder at 100, ring mod at 0; CC 1          |
| Ring Freq      | 5–5000 Hz    | 220 Hz  | Oscillator frequency; CC 2. Pad Y axis       |
| Ring Ratio   | 0.5–8        | 2.0     | 16 musical steps, −12 to +35 st; CC 3        |
| Ring Shape   | sine…square  | sine    | Band-limited with polyBLEP                   |
| Ring Depth   | 0–100 %      | 100 %   | 0 % is a clean pass-through                  |
| Bands        | 4–64         | 24      | Filter bank size; CC 4                       |
| Spread       | 1–8 oct      | 5 oct   | How much spectrum the bank covers           |
| Attack       | 1–200 ms     | 12 ms   | Envelope follower, time-based                |
| Release      | 20–1000 ms   | 180 ms  | Envelope follower, time-based                |
| Formant      | −12–+12 st   | 0 st    | Moves formants, not the band grid            |
| Tilt         | ±12 dB/oct   | 0       | Per-band gain, leans the spectrum            |
| Carrier      | input/osc    | input   | What the vocoder analyses (3/4); CC 5       |
| Sync         | off/on       | off     | Ring mod feeds the vocoder's carrier        |
| Width        | 0–100 %      | 50 %    | Ring mod widens, vocoder stays centred      |
| Drift        | 0–50 %       | 4 %     | Slow ±40 cent wobble on the carrier         |
| Conv Mix     | 0–100 %      | 0 %     | Fades ring/vocoder pair against convolution |
| Src Feedback | 0–95 %       | 0 %     | Output recirculated into the 1/2 frames     |
| IR Feedback  | 0–95 %       | 0 %     | Output recirculated into the 3/4 frames     |
| Bypass       | off/on       | off     | DSP keeps running                            |
| Randomise    | trigger      | —       | Builds a new plausible patch                 |

Width exists because the two engines are not equally stereo. The ring modulator
inherits the modulator's own stereo image; the vocoder is inherently mono. So
widening pushes the ring mod out and leaves the vocoder centred, which means the
control does something useful at every Mix position instead of only at the
ring-mod end.

### Drift routing

Route Drift's MIDI output to Voxmod's MIDI input. With default settings CC 1 →
Mix, CC 2 → Ring Freq, CC 3 → Ring Ratio, CC 4 → Bands, CC 5 → Carrier. CC 1–4
are Drift's default lanes; each control's CC number is a dropdown in the panel
and `off` disables that override. CC Channel selects which MIDI channel is
listened to.

### Randomise

Draws carrier frequency, ratio, shape, band count, spread, formant and tilt from
the seed, rounded to values someone would actually pick. It advances the seed so
a second press differs, and it is reproducible from the seed alone. Mix, Conv
Mix, the three Levels and Bypass are left alone, so randomising mid-performance
does not change the balance between the engines.

### Status

The panel reports the generated ring-oscillator frequency, a Sibilance meter
showing the share of carrier energy in the top bands, and the band gate's
reduction in dB. The frequency is the vocoder carrier only with `Carrier = internal
osc` and `Sync` off; it is not a pitch estimate of the signal on 3/4. Sibilance
is worth watching: a carrier with a lot of energy above the top of the bank
cannot be analysed well, and the meter switches to a warning colour above 0.7
because a bright carrier will produce a thin, hissy result.

Core DSP, deterministic tests, the VST3 target, the panel and the catalog
screenshot are complete. Host validation in REAPER is pending.