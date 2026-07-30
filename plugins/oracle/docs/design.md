# Oracle design

The core owns a fixed 2048-sample feature window and fixed MIDI output storage.
Non-finite audio is treated as silence and counted. CC output is limited to one
feature set per block. Response notes use a quarter-note refractory interval so
self-listening patches cannot create unbounded event feedback.
