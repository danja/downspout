# Loopdelay

Loopdelay is a stereo delay and capture looper designed to sit directly after
`t_mix.vst3`. Time can be set freely from 20–4000 ms or synchronized to the
host BBT clock from a quarter beat through four bars. Delay mode provides
filtered feedback and ping-pong crossfeed; Loop mode captures one selected
length and then applies feedback-controlled memory plus live overdubbing.

For automatic production, route MIDI from a controller generator to the
Loopdelay track. CC 30 controls time and CC 31 controls feedback on any MIDI
channel. Incoming CC temporarily takes over the saved panel value; **Release
MIDI** returns both controls to their saved defaults.

Recommended effect order: `T-Mix → Loopdelay → Guardian`.
