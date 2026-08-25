---
title: Gater
order: 50
bundle: gater.vst3
kind: Audio effect
role: MIDI-controlled switcher
screenshot: /assets/plugins/gater.png
summary: MIDI-controlled audio switcher routing one input to one of two outputs based on note input.
---

## Opinion

I thought I needed this then discovered that I hardly use it... Very forgetable, but should be very useful in Transmission for generative material.

## Functionality

Gater provides a simple way to toggle stereo audio between two output pairs.
It listens for MIDI Note On messages: even note numbers route audio to
Output 1, while odd note numbers route audio to Output 2.

### Status

Initial implementation - functional.
