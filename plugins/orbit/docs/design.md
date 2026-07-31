# Orbit design

Trajectory position is derived from absolute host quarter position. Random
walk endpoints are seed hashes with smooth interpolation, so seeks reproduce
the same path. The effect uses ordinary stereo panning and filtering; it does
not claim binaural or HRTF processing. Delay storage is preallocated.

The UI names all trajectory types, labels timing in beats, disables the seed
outside random-walk mode, and draws the selected path with a live position
marker. Pan and effective distance remain explicit read-only values.
