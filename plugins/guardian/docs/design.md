# Guardian design

The wrapper reports the selected look-ahead in samples through DPF latency.
Delay memory is allocated only on sample-rate changes. The callback replaces
non-finite input with silence, increments a fault counter, applies an 8 Hz DC
blocker, uses immediate attack and bounded release gain, then clamps output to
the selected ceiling. Reset clears latched diagnostics and audio history.
