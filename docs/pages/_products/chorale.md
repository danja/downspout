---
title: Chorale
order: 239
bundle: chorale.vst3
kind: Audio effect
category: processor
group: space
role: Stereo chorus / flanger / phaser
screenshot: /assets/plugins/chorale.png
capabilities: [audio input, audio output, MIDI input]
summary: A stereo modulation effect with the three Juno-style chorus voicings, a Custom chorus of one to four voices, a fed-back flanger and a moving-notch phaser, plus Tone, Width and Mix, with CC control of Rate, Depth, Width and Mix.
---

## Opinion

Pretty good. Hard _not_ to use at max settings, is fun.

## Functionality

Each side is low-passed and written to its own delay line (**Input: Stereo**, the default, so a stereo source keeps its image; **Mono sum** sums L and R into one line as the Juno original does). **I**, **I+II** and **II** read it through the fixed Juno sweeps (0.513 Hz and 0.863 Hz,
1.5 to 5.4 ms), a triangle sweep read in opposite directions for left and right. **Custom** has **Rate**, **Depth** (the sweep either side of the centre) and
**Delay** (the centre), with one to four **Voices** spread round the cycle. **Flanger** reads one short swept tap and feeds it, damped, back into the delay
line at **Feedback** (0-92 %) so the echoes decay into metallic combs. **Phaser** runs 2-12 modulated allpass **Stages** per side, with moving notches.
**Tone** opens the filters on the way in and out, **Width** blends the wet signal from mono to full stereo, and **Mix** is equal power. CC 1-4 on the CC
channel drive Rate, Depth, Width and Mix until you move that control again. The panel shows the current sweep and the values a controller has taken over.

### Status

Core covered by tests (270 checks): the Juno modes against an independent transcription of the source algorithm, the Custom delay, depth, rate and voice
spread by correlation, the flanger's echo train and the phaser's moving notches, width, tone, controllers, block-size independence and idle behaviour. Voicing of
Custom (sine sweep, voice spacing, level) is by ear. Not yet listened to or validated in a host; the new Flanger and Phaser modes also await a host listen.
