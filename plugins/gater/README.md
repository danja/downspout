# Gater

Gater is a MIDI-controlled stereo audio switcher.

## Functionality

It takes one stereo audio input and routes it to one of two stereo audio
outputs, determined by incoming MIDI Note On events.

### MIDI Control

- **Even Note Numbers**: Route audio to Output 1.
- **Odd Note Numbers**: Route audio to Output 2.
