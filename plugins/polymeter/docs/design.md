# Polymeter design

All scheduling uses absolute quarter-note boundaries and sample offsets inside
the current block. Lanes share one VST3 event bus and separate themselves by
note and channel. Fixed event and active-note arrays bound polyphony and queue
use; stop, rewind, and transport reset release outstanding ratchets.

The UI keeps every lane together and previews its Euclidean pattern and current
playhead. Note names, channels, probability, accent, ratchets, and phase drift
remain visible without losing the length/pulse/rotation relationship.
