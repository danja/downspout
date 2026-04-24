# e-mix porting notes

## Transport behavior

One source detail matters: the LV2 README claims `e-mix` should pass audio
through when transport is stopped or unavailable, but the source processor does
not do that. Instead it keeps advancing an internal fallback bar clock using
the last known BPM and meter, or the defaults `120 BPM` and `4/4`.

The `downspout` core currently preserves the processor code, not the README.

## Channel layout

The LV2 shell advertises eight optional inputs and outputs. That shape is not a
good default for the VST3 target, so the wrapper uses stereo I/O. The portable
core still supports variable channel counts for future reuse.

## UI direction

The old LV2 UI was technically adequate but not explanatory. The VST3 UI is
therefore allowed to diverge visually as long as it remains behaviorally
traceable to the same five parameters.
