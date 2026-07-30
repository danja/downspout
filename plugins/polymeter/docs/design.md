# Polymeter design

All scheduling uses absolute quarter-note boundaries and sample offsets inside
the current block. Lanes share one VST3 event bus and separate themselves by
note and channel. Fixed event and active-note arrays bound polyphony and queue
use; stop, rewind, and transport reset release outstanding ratchets.
