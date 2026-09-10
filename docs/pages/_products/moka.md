---
title: Moka
order: 156
bundle: moka.vst3
kind: Instrument
role: Modal hit-object synth
screenshot: /assets/plugins/moka.png
summary: Stereo modal synth for struck objects — xylophone, glockenspiel, woodblock, glass bowl, metal sheet, and tube — with ten controls and ten factory presets.
---

## Opinion

The most playable struck-object voice in the set. Metal Sheet into a long Decay is an instant thunder plate; Glass Bowl with soft Mallet sits under pads without fighting them.

## Functionality

Moka renders each note as a bank of eight damped sine modes excited by a
short mallet transient. Six instrument tables (Xylophone, Glockenspiel,
Woodblock, Glass Bowl, Metal Sheet, Tube) set the partial ratios, levels,
and decay multipliers; the ten panel controls then morph excitation,
resonance, ring-out, and stereo spread around that table.

### Parameters

| Parameter  | Range            | Default   | Notes                                                     |
|------------|------------------|-----------|-----------------------------------------------------------|
| Instrument | 6 models         | Xylophone | Selects the modal table (presets or automation)           |
| Decay      | 0–100 %          | 55 %      | Overall resonance time, 0.15x–3x                          |
| Mallet     | 0–100 %          | 75 %      | Beater hardness: partial brightness plus click level      |
| Tone       | 0–100 %          | 65 %      | Resonator lowpass                                         |
| Spread     | 0–100 %          | 30 %      | Inharmonic stretch, 0.6x–1.5x of table ratios             |
| Position   | 0–100 %          | 45 %      | Strike point: edge (bright) to centre (round)             |
| Voices     | 1–12             | 4         | Polyphony cap with oldest-voice stealing                  |
| Level      | 0–100 %          | 70 %      | Output gain                                               |
| Release    | 30 ms–1.5 s      | 97 ms     | Note-off ring-out, choke to long tail                     |
| Width      | 0–100 %          | 70 %      | Stereo spread of the modal partials                       |

### Presets

Xylophone, Glockenspiel, Temple Blocks, Glass Bowl, Frost Glass,
Metal Sheet, Thunder Plate, Tube Flip-Flop, Dark Tube, Soft Xylo.
A single preset menu writes all ten controls; any tweak shows `Custom`.

### Status

Core DSP, tests, VST3 target, and catalog screenshot are complete.
Host validation in REAPER remains.
