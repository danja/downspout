# Loopdelay design

The portable core owns all audio, transport, MIDI, and loop state. The DPF
wrapper only translates host buffers, BBT, parameters, MIDI events, and saved
text state. Delay buffers are allocated in `prepare()` and never resized by
the audio callback.

Free time covers 20–4000 ms. Synchronized time supports quarter-beat,
half-beat, one-beat, two-beat, one-bar, two-bar, and four-bar values. A beat is
derived from the BBT denominator and a bar from the complete host meter; 120
BPM and the available meter are used while valid BBT is unavailable. Actual
storage is capped at 16 seconds.

CC 30 controls time and CC 31 controls feedback on every MIDI channel. In Free
mode the time mapping is exponential; in Sync mode it selects one of the seven
musical lengths. MIDI takeover is transient and deliberately excluded from
saved state. The loop audio itself is also transient; only panel parameters
are serialized.

Entering Loop mode, changing effective loop time, or pressing Clear starts a
fresh one-pass capture. Host stop, loop boundaries, and rewinds do not erase a
captured loop. Tempo or meter changes update synchronized length and therefore
start a new capture when the resulting length changes.
