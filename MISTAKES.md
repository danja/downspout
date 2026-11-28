# Mistakes log

New, unresolved entries only. Resolved lessons are promoted to `AGENTS.md` and removed from here.

Format: what happened, root cause, prevention.

## Open

## energise: BBT helper assertions guessed the wrong bar-phase convention (2026-11-28)
- **What happened:** three new assertions failed in a row: `secondsToNextBar()` at bar start was asserted as zero, two bars at 60 BPM were expected to fit four bars, and a 45 BPM bar was treated as longer than eight seconds.
- **Root cause:** the test expectations were written from intuition instead of the formula: a bar-start position is a full bar *away* from the next bar, bar length is `4 * 60/BPM`, and the eight-second cap limits the bar count.
- **Prevention:** derive BBT expectations from `beatSeconds`, `beatsPerBar` and the hard cap, then assert the computed frames against a named BPM/bar case before wiring transport code.

## energise: switch-mapping test assumed truncation, not rounding (2026-11-28)
- **What happened:** `audioOutMode(1.6f)` was asserted to be `Pass`, but the helper rounds to the nearest enumeration value, so it correctly maps to `Render`.
- **Root cause:** the test encoded a mental model of integer truncation rather than the helper's published `value + 0.5f` mapping.
- **Prevention:** write switch-mapping cases from the helper formula and include values on both sides of each boundary.

## energise: activation cleared the loop length along with runtime state (2026-11-28)
- **What happened:** `activate()`/`sampleRateChanged()` called `stopRuntime()`, which resets `loopFrames_` to zero; the saved break was still in the processor, but Audio Out = Render passed the inputs instead of looping the patch until another action rebuilt the loop.
- **Root cause:** `stopRuntime()` mixes patch-derived playback state with purely transport state, and neither lifecycle hook re-synced the loop length from the stored buffers.
- **Prevention:** after clearing runtime state in host lifecycle hooks, re-derive loop state from the persisted processor buffers; keep patch data untouched in `activate()`.

## padseq: Page stepper and top Page buttons looked inert (2026-11-27)
- **What happened:** clicking the Page stepper or top Page buttons left the displayed page unchanged, so page selection appeared broken even though the processor changed pages.
- **Root cause:** `MagnetoUI::commit()` updates only its private 64-value array, and the JACK standalone does not echo UI writes through `parameterChanged()`. PadSeq's controls draw from a separate `allValues_` mirror that was never updated by UI writes. The fixed 0-3 page range also disagreed with the processor's effective `pageCount(steps)-1` bound.
- **Prevention:** override `sendValue()` to mirror every UI write into the plugin's own value array (and the base mirror), clamp page against the current Steps value there, and treat processor-owned status outputs as the source of truth for effective page/scale/view.

## padseq: Clear looked inert in the UI after an edit (2026-11-27)
- **What happened:** clicking a grid cell and then the top Clear button left the cell visibly active even though the processor had cleared the pattern.
- **Root cause:** the grid draws editable cell parameters, but Randomise/Clear change the processor's cells internally; DPF does not echo DSP-internal parameter changes back to the UI. The status-cell outputs carried the new values but were not mapped back to the editable cell values.
- **Prevention:** mirror status-cell changes onto the corresponding editable cell values in `parameterChanged()`, and clear local cell values immediately when the UI sends Clear.

## padseq: LED Out selector did not respond and default still echoed LED notes (2026-11-27)
- **What happened:** the UI's new LED Out buttons could not be clicked and showed Off, while a fresh plugin still emitted channel-1 LED notes by default.
- **Root cause:** `MagnetoUI` mirrors only 64 values; `drawSegments()`/`commit()` were used with parameter index 348, so selection was drawn from and committed to an out-of-bounds slot. The default had been left at `Note+CC` to match Plank, which kept the unwanted traffic on a shared MIDI output.
- **Prevention:** any control above `MagnetoUI::kMaxValues` must be drawn and hit-tested by the plugin against its own full value array and sent with `sendValue()`; default a controller's lighting transport to the shared-output-safe choice (`SysEx`), with note/CC available only as an explicit opt-in.

## padseq: appended LED Out parameter collided with status cell 1 (2026-11-27)
- **What happened:** appending `ParamId::ledTransport` at `statusCellStart` shifted the read-only status block and produced 29 test failures (`parameter count` 286 instead of 349); after moving it after all 64 status cells, writes still had no effect and six LED-mode tests failed.
- **Root cause:** the enum was inserted at the status base instead of past the full 64-cell block, and `setParameter()`/`clampPatch()` only traversed `kInputParameterCount`, which stops before the status block and therefore skipped the appended control.
- **Prevention:** when preserving host indices, append at `statusCellStart + kGridCellCount` and add a parameter-count assertion; any control appended after read-only outputs needs an explicit writable index check and clamp pass (or a shared predicate) so it still behaves like an ordinary input.

## padseq: channel-1 LED traffic reached a shared synth (2026-11-27)
- **What happened:** after adopting Plank's note/CC LED path, the Launchpad lit correctly but the MIDI output broadcast channel-1 LED notes/CCs, so a downstream instrument heard the scan as music.
- **Root cause:** one MIDI output carried both generated notes and LED commands, and the receiving instrument accepted all channels by default. The earlier bulk-SysEx transport avoided this but was filtered by the host.
- **Prevention:** expose an LED transport choice (`Off`, `SysEx`, `Note+CC`) and make refresh *and* Clear respect it so SysEx mode emits no channel-1 note/CC messages at all; default it to `SysEx` for a shared MIDI output and document note/CC plus channel-filter fallbacks for hosts where SysEx is filtered.

## padseq: Launchpad stayed in session mode and then did not light (2026-11-27)
- **What happened:** a connected Launchpad stayed in session mode and its pads/top/side buttons did nothing with LED Feedback at its old default Off. After moving the handshake out of the LED-off guard, it still behaved differently from Plank because lighting relied on a model-specific bulk SysEx that the host may drop.
- **Root cause:** two divergences from the known-working Plank path: the handshake was gated by LED feedback (fixed first), and PadSeq's LED transport/default was not Plank's channel-1 note/CC with feedback on.
- **Prevention:** when a controller "does nothing", compare the full input/mode/output path with a known-working plugin, including parameter defaults and message types, not just the handshake. Send the mode SysEx regardless of feedback, use short channel-1 note/CC LEDs for routine colours, keep generated notes on another channel, and assert both paths in core tests.

## guardian: removing diagnostics also removed the curve marker (2026-11-26)
- **What happened:** the transfer-curve peak marker disappeared because it read `status_peak`, which was deleted with the Safety Monitor outputs.
- **Root cause:** the marker's data dependency was not traced before removing the output parameter; no test or screenshot check covered a live marker.
- **Prevention:** grep the UI for every output parameter before deleting it, and check that a fresh screenshot still shows the marker (with a demo value when the standalone has no audio).

## guardian: clip-ramp denominator used the remaining run only (2026-11-26)
- **What happened:** the interpolation test would have seen ramp values beyond the ceiling for the last samples of a clipped run; the formula was corrected before the test run.
- **Root cause:** the denominator was `exitOffset + 1` (samples remaining) instead of the whole run, `run + exitOffset + 1`.
- **Prevention:** derive the ramp from total run length, then assert every intermediate ramp point (not just the first) against the endpoints.

## guardian: program release affected attack, not recovery (2026-11-26)
- **What happened:** the first draft chose the fast release coefficient from the current limiting state, so it never applied while the gain was recovering after a peak.
- **Root cause:** attack and release directions were collapsed into one coefficient; recovery starts after `target` returns to 1.
- **Prevention:** branch on `target < gain` for attack and compare internal `gain` after a short transient for release; keep the hold counter through recovery.

## guardian: core tests forgot the look-ahead delay (2026-11-26)
- **What happened:** the single-sample input-gain and instant-attack assertions failed because the default 5 ms look-ahead delayed the first output.
- **Root cause:** the old tests explicitly zeroed look-ahead; the new fixture inherited the default.
- **Prevention:** derive single-sample fixtures from the parameters under test and set every delay-producing control explicitly.

## voxmod: bypass stopped muting during the three-way blend (2026-11-25)
- **What happened:** `testBypassIsTransparent` failed after adding Conv Mix; the bypassed plugin still passed the ring and vocoder engines.
- **Root cause:** the new `legacyAmount` was derived only from `1 - convAmount`, dropping the existing `bypassed ? 0 : ...` condition when the amounts were refactored.
- **Prevention:** derive every blend branch from the bypass flag in one place, and rerun the whole plugin suite (including bypass) after changing any amount expression.

## voxmod: direction test for commutative frame multiplication (2026-11-25)
- **What happened:** a new test asserted that swapping the source and IR pairs changes the convolution output; it failed because the outputs matched.
- **Root cause:** frequency-domain frame multiplication is commutative, so cross-synthesis has no source/IR direction; the test encoded an assumption from linear convolution with a fixed IR.
- **Prevention:** derive the algebra before writing an observable-effect test; prove the routing contract with silence on one pair instead of swapping symmetric operands.

## voxmod: limiter on the whole mix squashed the vocoder (2026-11-25)
- **What happened:** after adding a final-output peak limiter, `testFormantShiftChangesOutput` failed because both shift directions were clamped to the same 0.89 peak, and a normal vocoder patch sat at the limiter permanently.
- **Root cause:** the limiter was placed after all three engines, but only the convolution feedback loop needed stabilising; the legacy vocoder is intentionally hot and was being compressed at its default level.
- **Prevention:** scope stabilisers to the engine that needs them. Keep an unchanged legacy-behaviour test (formant/mix pass-through) as the guard when adding global output stages.

## voxmod: normalisation test kept the old delta-IR expectation (2026-11-25)
- **What happened:** after normalising the frame product by `kFft * kFrame`, the delta-IR frame test failed by a factor of 4096 until its expected value was re-derived.
- **Root cause:** the old expectation baked in the previous `0.5 / kFft` scale instead of the new inverse-transform algebra: a delta IR gives the windowed source divided by the frame length.
- **Prevention:** derive test expectations from the transform definition after changing a normalisation constant; render one probe point by hand before loosening tolerances.

## voxmod: ring oscillator phase used a hard-coded 48 kHz (2026-11-26)
- **What happened:** the ring carrier shifted pitch with the host sample rate and the new sample-rate test failed at 44.1/96 kHz.
- **Root cause:** `oscillatorSample()` divided frequency by a literal 48000 instead of the block's sample rate, so its phase advance and polyBLEP width were wrong off 48 kHz.
- **Prevention:** pass the sample rate into every phase-incrementing oscillator and pin the generated frequency across 44.1/48/96 kHz with a settled zero-crossing test.

## voxmod: routing audit relied on comments and a symmetric test (2026-11-26)
- **What happened:** the DSP, port metadata and UI agreed on 1/2 source and 3/4 carrier/IR, but `carrierSource` was commented as input 1/2, Sinevox's README described Voxmod's slots backwards, and the vocoder carrier test passed even with the pairs swapped.
- **Root cause:** routing was documented in prose rather than pinned by a distinguishable signal, and stale comments survived the earlier input swap.
- **Prevention:** add frequency-discriminating tests for vocoder carrier/source roles and stereo channel pairing; keep port comments, README cross-references and UI labels in the same audit whenever routing changes.

## voxmod: wrapper probe used brace-initialised vectors (2026-11-27)
- **What happened:** the first wrapper probe aborted with `malloc(): corrupted top size` because the audio buffers had one element each while `run()` wrote 256 frames.
- **Root cause:** `std::vector<float> v { kFrames }` selects the initializer-list constructor, so it creates one float with value 256 instead of 256 zeroes.
- **Prevention:** allocate buffers with parentheses (`std::vector<float> v(kFrames)`), and run any new rendering probe through ASan before trusting a heap symptom.

## Template

## <plugin/area>: <short description> (YYYY-MM-DD)
- **What happened:** observable failure and how it was found.
- **Root cause:** the specific assumption or missing check.
- **Prevention:** the test, build step or design rule that prevents a repeat.
