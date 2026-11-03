# Wheelhouse design

A tonewheel organ. Clean-room: the design comes from the published behaviour of this kind of organ (footage ratios,
3 dB drawbar steps, wheel foldback, single-trigger percussion, scanner vibrato), not from any source code. The JSFX port of
setBfree (`Lord of the Rinks`, GPL) in `DSP-Ports-JSFX` was read only as a reference for what an organ plugin should expose;
nothing is translated from it, and Downspout stays MIT.

## Signal flow

```
91 wheels (sine, phase always running)
  |  gain[division][wheel]  (smoothed contacts: 1.5 ms attack, 3 ms release)
  v
upper bus (+ percussion)   lower bus   pedal bus
  |            |              |
  +-- scanner (routed manuals) --+      pedal goes round
  v
(sum) x kMasterGain x swell^2 + key click -> drive (tanh) -> tone (one-pole) -> x volume -> L / R
```

- **Wheels.** Wheel `w` (0-90) is MIDI note `24 + w`, equal-tempered at A = 440 Hz. A sine table with linear interpolation
  plays each; the phases start at golden-ratio spread values so a chord never lines up into one peak. Every wheel turns whether
  or not it is used, so the phase relation between notes is fixed, as on the instrument.
- **Keys and footages.** A key closes one contact per footage. The wheel is `key + offset` with offsets 16' -12, 5 1/3' -5, 8' 0,
  4' +12, 2 2/3' +19, 2' +24, 1 3/5' +28, 1 1/3' +31, 1' +36 (pedals: 16' -12, 8' 0). Above the top wheel (note 114) the wheel
  folds back by octaves until it fits. `gain[division][wheel]` is the sum over held keys of the drawbar amplitudes that land
  on that wheel, so two footages sharing a wheel add. The targets are recomputed at every block and every key event.
- **Drawbars.** Position 0 is off; positions 1-8 are `10^((p - 8) * 3 / 20)`, so 8 is 0 dB and each step is 3 dB.
- **Contacts.** A key does not switch a wheel on instantly: the gain follows its target with a 1.5 ms attack and 3 ms release,
  which is also what removes the pop. A wheel whose gain and target are both zero is not summed. Gains snap to zero below 1e-7,
  so an idle organ is exactly silent (no denormals).
- **Percussion.** Upper manual only. A second wheel per key plays the 2nd (key + 12) or 3rd (key + 19) harmonic under an
  envelope `percEnv`, which jumps to 1 when a key is pressed while no upper key is held (a legato key does not retrigger) and
  decays exponentially to -60 dB over 0.35 s (fast) or 1.5 s (slow). Soft is half the level of normal. While percussion is on
  the upper 1' drawbar is silenced. The envelope always runs, so switching percussion on mid-phrase does not retrigger it.
- **Scanner.** The manuals routed to it (Vibrato Upper / Lower) are summed into one signal that goes through a delay line
  modulated by a triangle LFO (Scanner Rate): delay `depth * (1 + tri)`, depth 0.20 / 0.35 / 0.55 ms for 1 / 2 / 3, linearly
  interpolated. **V** modes output the delayed signal; **C** modes output half dry and half delayed. The right channel's LFO
  is offset by `Width * 0.25` cycle. Pedals and unrouted manuals bypass the scanner.
- **Key click.** Every key press and release restarts a 2 ms decaying envelope on high-passed noise (a fixed xorshift sequence,
  so renders repeat), scaled by Key Click. It sits before the drive.
- **Swell and master.** `kMasterGain` is 0.05, calibrated so a four-note 888 chord peaks near 0.25 at the default Volume. CC 11
  scales by `swell^2`, before the drive, so quiet swell is also cleaner, as on the instrument.
- **Drive.** `tanh(k x) / k` with `k = 1 + 7 * Drive`: unity for small signals, bounded by `1 / k`. Drive 0 skips the stage.
- **Tone.** One-pole low-pass, cutoff `1500 * (16000 / 1500)^Tone`; at 100 % the stage is skipped.

## Assumptions (host mapping and where it differs from the instrument)

- **Tuning.** Equal temperament. A real tonewheel set is tuned by gear ratios and is slightly off equal temperament; this is
  not modelled, and neither is the wheels' leakage or their waveform impurity (they are pure sines).
- **Foldback.** Octave folding at the top matches the behaviour, but the exact set of keys that fold on a real console is not
  checked; the wheel set here is simply notes 24-114.
- **Voicing constants are by ear:** the percussion times and levels, the scanner depths, the click level, the master gain, the
  tone range and the drive law. They are not measurements of a Hammond.
- **Velocity** is ignored. **Sustain pedal** (CC 64) is ignored: an organ has none.
- **MIDI.** One MIDI input. Each division reads its own channel; a channel set to 0 turns it off, and two divisions may share a
  channel. Note-on with velocity 0 is a note-off. CC 11 from any channel is the swell. CC 123 and 120 release the keys of
  their channel's divisions.
- **State.** Every setting is an ordinary parameter (saved by DPF by symbol), so there is no `WANT_STATE`. Held keys, wheel
  phases, percussion, click, scanner and filter state are runtime state; `activate()` clears them and never touches the patch.
- **Latency.** None.
- **Registrations.** The panel's Jazz / Full / Flute / Reed buttons are one-shots in the UI that set a manual's nine drawbars as
  ordinary parameters; nothing is applied in DSP, so a saved project is never overwritten on load.

## Not built

A Leslie or other rotary speaker (use a separate effect after it), reverb, hardware drawbar CCs, key transpose or split, a
second percussion level or a "normal volume drop" when percussion is on, tonewheel leakage and tuning error, per-note key click
character, a sustain pedal, and a CPU-saving voice limit (the cost is flat: see below).

## Cost

All 91 wheels always advance; only wheels with a contact are summed. With every key of the upper and lower manuals held and
every drawbar out (122 keys), 10 s renders in about 1.8 s on the development machine (a busy-machine upper bound), about 18 %
of one core at 48 kHz. A normal chord is far below that.

## Tests

`tests/wheelhouse_core_tests.cpp` (104 checks). Independent expectations: a hand-written table of the wheel each footage plays
at the lowest and highest key; amplitudes measured with a Hann-windowed Goertzel evaluated at the exact frequency. Covered: the
wheel mapping and foldback; each footage sounding at its own pitch only; every drawbar position 0-8 against 3 dB steps; the
folded footages at the top key; the three divisions and their channels, channel 0, shared channels, key range edges and the
pedal range; release to exact silence, all-notes-off on the right channel only, repeated note-ons; swell, volume; percussion
(harmonic, decay curve against `10^(-3 t / T)`, slow vs fast, soft vs normal, off, legato vs detached trigger, 1' silenced,
upper only); the scanner sidebands against the FM formula (`J1(b) / J0(b)` for each depth), V1 < V2 < V3, chorus, rate,
routing of each manual and the pedals, Width; key click (on, off, linear, none when 0, over in 30 ms); drive (clean path adds
nothing at 3.5 f, harmonics when driven, bounded by `1 / k`); tone against the one-pole response at four keys; level bands;
block-size independence (bit-identical at five block sizes) and repeatability; a drawbar moved on a held key; the cost;
idle silence and reset; clamping, table defaults and pinned parameter ids. Mutation-checked: a 2 dB step, no 1' silencing, a
wrong scanner depth and no foldback each make several checks fail.
