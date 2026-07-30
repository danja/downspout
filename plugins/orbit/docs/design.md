# Orbit design

Trajectory position is derived from absolute host quarter position. Random
walk endpoints are seed hashes with smooth interpolation, so seeks reproduce
the same path. The effect uses ordinary stereo panning and filtering; it does
not claim binaural or HRTF processing. Delay storage is preallocated.
