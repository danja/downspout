# Resonance Garden design

Delay storage is allocated only during sample-rate preparation. Processing uses
fixed voices, bounded feedback, denormal suppression, finite-input recovery, and
soft-clipped output. MIDI note replacement is deterministic and held notes
decay cleanly after release.

The UI groups resonance, tuning, excitation, and output controls and visualizes
the bounded eight-voice bank. Internal pitch classes and scales use musical
names; active voices and output peak are read-only meters.
