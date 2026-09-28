# Ghost

`ghost.vst3` — audio-triggered ghost-note generator.

Ghost listens to incoming audio and, in time with the transport BBT grid,
inserts MIDI note events: ghost drum beats on MIDI channel 10 (kick 36 on the
quarter, ghost snare 38 off it) or ghost notes on a selectable channel (Notes
mode walks a pentatonic-minor ladder from Base Note). Onset accents quantise to
the next 16th slot; quieter fills land on off-16th slots with probability
Density. Drag pushes every ghost late inside its slot — the laid-back feel the
name promises.

## Using it

Ghost is an inline tap: stereo audio passes through untouched (sanitised) and
incoming MIDI passes through unless consumed as routing CCs.

**The four musical controls** (INBOX TBD resolution — these are the four Drift
lanes, CC 1–4 by default):

| CC | Control | Range | Default |
|----|---------|-------|---------|
| 1 | Sensitivity | 0–100 % | 50 % |
| 2 | Density | 0–100 % | 35 % |
| 3 | Velocity | 1–127 | 90 |
| 4 | Drag | 0–100 % | 15 % |

Route Drift's MIDI output to Ghost's MIDI input and the four lanes play the
ghost pattern with no configuration. CC 0 disables the override for that
control. Routing CCs on the CC channel are consumed; all other MIDI is
forwarded to the output.

**Modes.** Drums forces channel 10. Notes uses Channel (default 10, set 1–16)
and Base Note (drums: ghost voice anchor; notes: scale root, shown with note
names in the panel). Pass MIDI toggles forwarding; Seed drives the deterministic
fill/voice streams — same seed, same ghosts.

**Transport.** Ghosts only fire while the transport runs with valid BBT. Loop
jumps and seeks reset the slot cursor (no burst); emission is capped at 8
ghosts per block plus forwarded input.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_GHOST=ON
cmake --build build --target downspout_ghost_core_tests ghost-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_ghost_core_tests --output-on-failure
```

Core tests cover stopped/invalid transport silence, impulse-triggered onsets,
channel-10 drum voices, Notes-mode channel routing, the density gate,
determinism, loop-jump boundedness, non-finite sanitisation, clamping, and
state round trip.

## Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER remains. Live onset/level
metering in the panel (output status parameters) is explicitly future work.
