# Spindown design

A tape-stop and tape-start effect. Clean-room: the design comes from how a tape transport behaves, not from any source code.
The JSFX port of cStop in `DSP-Ports-JSFX` (GPLv3) was read only as a reference for the idea; nothing is translated from it,
and Downspout stays MIT.

## Signal flow

The stereo input is written into a ring buffer (10.5 s at the host rate, allocated in `prepare()` from the constructor and
`sampleRateChanged()`, never in the audio callback). A read head runs behind the write head by `delay` samples. Each sample:

```
progress += 1 / (StopTime * sr)     while stopped      (max 1)
progress -= 1 / (StartTime * sr)    while not stopped  (min 0)
speed     = (1 - progress)^Curve
delay    += 1 - speed                                    (0 .. 10 s)
out       = live * in + (1 - live) * hermite(ring, delay) * speedGain(speed)
```

- `progress` is a single state shared by the stop and the start, so releasing a stop part-way restarts from the speed
  reached and stopping again continues from there. The speed is continuous, there is no jump.
- `delay` is the time the head has spent slower than real time, so a linear stop over T leaves T/2 of lag (tested against
  the integral).
- The read uses a four-point Hermite interpolator, so a slowed head does not alias (slowing only lowers frequencies).
- `speedGain` fades the output out as the speed falls below 4 % so a head parked on one sample leaves no DC step. A stopped
  tape is silent.
- `live` is the crossfade to the live input. It drops to 0 as soon as the tape slows (immediately when the lag is under one
  sample, otherwise over 12 ms) and rises over 12 ms once the tape is back at full speed; at 1 the head is snapped to the write
  head (`delay = 0`) and the output is the input unchanged. Idle the effect is bit-identical to bypass, with zero latency.
- The lag is capped at 10 s: a stop held longer than that cannot overrun the buffer.

## Triggers

| Trigger | The tape is stopped when |
|---|---|
| Manual | `Stop` is on (or CC 4 is 64 and above) |
| MIDI | at least one note is held. Note-on with velocity 0 counts as note-off; CC 123 and CC 120 clear hung notes |
| Cycle | the transport is playing and the beat position within the cycle is in the last `Length` beats |

Cycle uses `cycleLength = beatsPerBar * (1, 2, 4 or 8)`, so it follows time-signature changes, and falls back to 4 beats per
bar when the host gives none. The beat position is evaluated per sample (block start plus `i * bpm / (60 * sr)`), so the
stop begins on the sample, not the block, and it does not depend on the block size. A stopped transport, or one with no
tempo, never engages the cycle. `Length` of a whole cycle or more is capped at the cycle (always stopped).

MIDI events are applied at their own frame. The plugin has one MIDI input and produces no MIDI.

## CC control

On the CC channel (0 = ignore CC; default 1, the same convention as the other Drift-driven plugins):

| CC | Control | Mapping |
|---|---|---|
| 1 | Stop Time | 20 ms to 4 s, logarithmic over 0-127 |
| 2 | Start Time | 20 ms to 4 s, logarithmic |
| 3 | Curve | 0.5 to 4 |
| 4 | Stop | 64 and above stops (used by the Manual trigger) |

A CC takes the control over (`overrideActive`) until the panel or host moves that parameter, which calls `releaseOverride`.
Drift's default lanes are CC 1-4, so it drives Stop Time, Start Time, Curve and Stop unconfigured.
The takeover values are runtime state; they are cleared by `activate()`, and are not saved.

## Assumptions (host mapping)

- **Transport**: the beat position is `(bar - 1) * beatsPerBar + (beat - 1) + tick / ticksPerBeat` from the BBT, as in
  `infra`. Hosts without BBT never engage Cycle; Manual and MIDI still work.
- **State**: every setting is an ordinary parameter (saved by DPF by symbol), so there is no `WANT_STATE`. The ring,
  head position, held notes and CC takeovers are runtime state; `activate()` clears them, and `sampleRateChanged()` reallocates
  the ring, neither touches the patch.
- **Latency**: none is reported. The lag is the effect, not a processing delay.
- **Offline render**: the result depends only on the transport position and the MIDI, so renders repeat exactly.

## Not built

A tone control that darkens the sound as the tape slows, a reverse (rewind) option, tempo-synced Stop and Start times, an
option to resume from where the tape stopped (the lag is dropped by the crossfade to live), and a trigger from audio level.

## Tests

`tests/spindown_core_tests.cpp` (85 checks): idle bit-identical at four block sizes; the speed curve measured from the slope
of a ramp input against a brute-force average of the formula, for four curves; every control having an effect (Curve, Stop
Time, Start Time, Length, Cycle); silence and finiteness at standstill; the lag at standstill (T/2) at 44.1, 48 and 96 kHz;
return to the live input exactly; continuity through an interrupted stop; a stop longer than the buffer; the unprepared
engine; the MIDI trigger (sample accuracy, held notes, all-notes-off, ignored in Manual); the Cycle schedule against
hand-written bar/beat expectations, in 3/4 and with no tempo, and a stopped transport; CC channels and takeover release;
reset; parameter clamping and the table/core defaults.
