# Mosaic design

RIFF/WAVE PCM16 and float32 files up to 60 seconds are loaded through state
callbacks, never from the audio callback. The audio core sees an immutable
four-slot pool and uses sixteen fixed voices. Missing or invalid files produce
silence and a visible status output. Playback performs no allocation or I/O.
