# Mosaic design

RIFF/WAVE PCM16 and float32 files up to 60 seconds are loaded through state
callbacks, never from the audio callback. The audio core sees an immutable
four-slot pool and uses sixteen fixed voices. Missing or invalid files produce
silence and a visible status output. Playback performs no allocation or I/O.

The DPF wrapper enables its file browser and marks all four slot states as
filename paths. The UI provides Load/Replace/Clear actions, filenames, pool
status, a region/grain preview, named trigger modes, and context-sensitive
autonomous controls. File selection remains a control-thread state operation.
