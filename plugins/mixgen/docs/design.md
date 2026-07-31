# Mixgen design notes

Mixgen is an original Downspout MIDI control generator.

## T-Mix contract

- MIDI CC 20-27 control T-Mix producer gains for strips 1-8.
- Values are linear gain: 0 is silence and 127 is unity.
- Mixgen emits all eight controllers at every transport-synchronized decision.
- MIDI channel is configurable; T-Mix accepts the contract on any channel.
- Disabling Mixgen emits unity for every lane at the next decision, restoring
  the manual T-Mix balance.

## Pattern behavior

- Random uses independent seeded decisions and repeats after Pattern Length.
- Quasi uses a seeded low-discrepancy sequence to avoid long accidental clumps.
- Euclidean distributes the requested density across the pattern length.
- Lane Spread rotates each lane progressively. Depth establishes the background
  gain, while Variation humanizes active-lane accents.
- Transport rewind, loop, or discontinuity resets scheduling without changing
  the deterministic pattern. Stopped or invalid transport emits nothing.

The portable core owns scheduling, deterministic pattern generation, and MIDI
events. The DPF wrapper only converts transport and writes host MIDI events.
