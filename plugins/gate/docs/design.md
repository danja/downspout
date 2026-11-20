# Gate: design notes

## Requirement

A standalone stereo noise gate with an optional stereo sidechain, implemented
clean-room after the feature set of tools such as ZamGateX2. One detector and one
gain envelope are shared by both main channels, so the stereo image is not
modulated independently. The plugin needs no MIDI, transport or state.

## Signal flow

1. **Sidechain source.** Internal uses main L/R; External uses audio inputs 3/4.
   A second stereo input pair is always declared to the host; when External is
   selected but nothing is connected, the detector sees silence and the gate
   closes. This is explicit rather than guessed, so an unconnected sidechain
   never silently falls back to the main signal.
2. **Sidechain HPF.** A one-pole high-pass at 20-500 Hz removes rumble from the
   detector only. At the 20 Hz floor the filter is bypassed exactly. The main
   audio path is never filtered.
3. **Detector.** The mean square of the two sidechain channels is smoothed with
   a 2 ms attack and 40 ms release, then read in dB. The slower fall stops a
   gate from chattering on short dips.
4. **State.** The gate opens when the detector rises above **Threshold** and
   closes only after it drops below `Threshold - Hysteresis`. A level between
   the two never changes the state. **Hold** counts down only while the level is
   below the close threshold; a recovery resets the counter.
5. **Envelope.** The shared open amount (0-1) follows **Attack** when opening
   and **Release** when closing. The applied gain is
   `10^(-Range x (1 - open) / 20)`, so a 60 dB range leaves -60 dB when closed.
6. **Output.** **Output** is a post-gate trim, and **Bypass** crossfades over
   5 ms to the untouched main input. Control changes take effect per block; the
   first block snaps the smoothers to the current patch.

All DSP is portable C++ with double-precision state. Non-finite input samples
are treated as silence and tiny state values are flushed to avoid denormals.

## Host mapping and state

- **Audio:** 4 in (main L/R, sidechain L/R), 2 out (gated main L/R).
- **MIDI / transport:** none. There is no CC map and no time-position use.
- **Latency:** none; the gate is sample-aligned and reports 0.
- **State:** none. Ten writable settings are ordinary host parameters. Three
  read-only outputs publish detector level, applied reduction and the live open
  state so the panel can show what the processor is actually doing.
- **Version:** v0.1.0, `gate.vst3`.

## Tests

`downspout_gate_core_tests` checks default values and clamps, loud pass-through,
full-range attenuation of a quiet signal, `Range = 0` transparency, external
sidechain behaviour, hysteresis, attack/hold/release timing, stereo linking,
bypass, output gain, patch persistence across `reset()` and block-size
invariance.

## Not done

- Visual acceptance of the 920x700 screenshot is pending. The capture succeeded
  and was checked as 920x700 with non-blank pixel statistics, but this agent
  cannot inspect images at full resolution in this session.
- No host listening, REAPER round trip or CPU measurement under load.
- No per-channel gates, MIDI/CC control or detector mode options (peak vs RMS).
