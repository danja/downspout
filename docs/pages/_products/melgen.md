---
title: MelGen
order: 50
bundle: melgen.vst3
kind: MIDI generator
category: generative
role: Melody generator
screenshot: /assets/plugins/melgen.png
summary: Phrase-aware melody generator with contour, answer, structure, color, range, follow, and strict subject/answer behavior.
---

## Opinion

Sometimes it's really good, produces a convincing melody line. Often it's a bit unpredictable, likes discords.

## Functionality

MelGen creates melodic MIDI phrases with controls for contour, range, rests,
call-and-answer shape, color, and cadence behavior. The follow control lets
incoming MIDI pull the generated line toward another part without simply
copying it. High Structure with Call Answer and low Leap/Rest gives it a
Fugue-friendly tonic subject and dominant answer region.

**Inertia** (default 0, which leaves the line unchanged) adds Narmour-style melodic
implication: after a leap of three scale degrees or more the next move tends to turn
back without overshooting, and after a step it tends to continue in the same direction.
Idea from [Subsequence](https://subsystem.co/subsequence).

### Status

Working, but the melodies generated are questionable. Algorithms under review.
