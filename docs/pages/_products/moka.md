---
title: Moka
order: 156
bundle: moka.vst3
kind: Instrument
category: instrument
role: Modal hit-object synth
screenshot: /assets/plugins/moka.png
summary: Stereo modal synth for struck objects — xylophone, glockenspiel, woodblock, glass bowl, metal sheet, and tube — with ten controls and eleven factory presets.
---

## Opinion

The most playable struck-object voice in the set. Metal Sheet into a long Decay is an instant thunder plate; Glass Bowl with soft Mallet sits under pads without fighting them. Kalimba is the one to reach for when a part wants to move — it shares the Glockenspiel's steel bar but drops the ring and thins the upper partials into a hollow thumb-picked tine.

## Functionality

Moka renders each note as a bank of eight damped sine modes excited by a
short mallet transient. Six instrument tables (Xylophone, Glockenspiel,
Woodblock, Glass Bowl, Metal Sheet, Tube) set the partial ratios, levels,
and decay multipliers; the ten panel controls then morph excitation,
resonance, ring-out, and stereo spread around that table.

Every note ramps in over the mallet's contact time, 1.5 ms on a hard beater
up to 6 ms on a soft one, so a note starts from silence instead of stepping
straight to full amplitude. Retriggering a held note crossfades its tail
rather than cutting it.

The panel plots the eight partial slots live, at the ratios and levels the
engine is actually using, and reads the fundamental's ring time on a dial —
so what the instrument table and Decay are doing together is visible rather
than inferred.

### Parameters

| Parameter  | Range            | Default   | Notes                                                     |
|------------|------------------|-----------|-----------------------------------------------------------|
| Instrument | 6 models         | Xylophone | Selects the modal table (presets or automation)           |
| Decay      | 0–100 %          | 55 %      | Ring time, 0.15x–3x of the model's base (1.1 s here)      |
| Mallet     | 0–100 %          | 75 %      | Beater hardness: partial brightness and contact time      |
| Tone       | 0–100 %          | 65 %      | Resonator lowpass, 900 Hz–11.9 kHz                        |
| Spread     | 0–100 %          | 30 %      | Inharmonic stretch, 0.6x–1.5x of table ratios             |
| Position   | 0–100 %          | 45 %      | Strike point: edge (bright) to centre (round)             |
| Voices     | 1–12             | 4         | Polyphony cap with oldest-voice stealing                  |
| Level      | 0–100 %          | 70 %      | Output gain                                               |
| Release    | 30 ms–1.5 s      | 97 ms     | Note-off ring-out, choke to long tail                     |
| Width      | 0–100 %          | 70 %      | Stereo spread of the modal partials                       |

### Presets

Xylophone, Glockenspiel, Temple Blocks, Glass Bowl, Frost Glass,
Metal Sheet, Thunder Plate, Tube Flip-Flop, Dark Tube, Soft Xylo, Kalimba.
A preset writes all ten controls; any tweak shows `Custom`.

**Pitch bend** is applied per MIDI channel (2 semitones by default, set per channel with
RPN 0) and voices are keyed by channel and note, so it follows the one-note-per-channel
output of [Retune](/downspout/plugins/retune/).

### Status

Core DSP, tests, VST3 target, and catalog screenshot are complete.
Host validation in REAPER remains.
