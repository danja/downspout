---
title: Chorale
order: 239
bundle: chorale.vst3
kind: Audio effect
category: processor
group: space
role: Stereo chorus
screenshot: /assets/plugins/chorale.png
capabilities: [audio input, audio output, MIDI input]
summary: A stereo chorus with the three Juno-style voicings and a Custom mode of one to four voices, plus Tone, Width and Mix, with CC control of Rate, Depth, Width and Mix.
---

## Opinion

Not yet auditioned in a host. I+II is the classic wide ensemble sound, a good first stop on a pad or a clean guitar; I alone is subtler, II is a quicker wobble.
Switch to Custom for slower, deeper or more voluminous choruses, and pull Width down to hear how much of the movement is the stereo spread.

## Functionality

Each side is low-passed and written to its own delay line (**Input: Stereo**, the default, so a stereo source keeps its image; **Mono sum** sums L and R into one line as the Juno original does). **I**, **I+II** and **II** read it through the fixed Juno sweeps (0.513 Hz and 0.863 Hz,
1.5 to 5.4 ms), a triangle sweep read in opposite directions for left and right. **Custom** has **Rate**, **Depth** (the sweep either side of the centre) and
**Delay** (the centre), with one to four **Voices** spread round the cycle. **Tone** opens the filters on the way in and out, **Width** blends the wet signal from
mono to full stereo, and **Mix** is equal power. CC 1-4 on the CC channel drive Rate, Depth, Width and Mix until you move that control again. The panel
shows each voice's delay sweeping, and the values a controller has taken over.

### Status

Core covered by tests (205 checks): the Juno modes against an independent transcription of the source algorithm, the Custom delay, depth, rate and voice
spread by correlation, width, tone, controllers, block-size independence and idle behaviour. Voicing of Custom (sine sweep, voice spacing, level) is by ear. Not yet
listened to or validated in a host.
