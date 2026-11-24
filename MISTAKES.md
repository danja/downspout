# Mistakes log

New, unresolved entries only. Resolved lessons are promoted to `AGENTS.md` and removed from here.

Format: what happened, root cause, prevention.

## Open

## bassgen: velocity test hit the wrong transport boundary (2026-11-24)
- **What happened:** `emittedVelocityFor(30) == 77` failed because the test used `playingTransport(0.249)` with `lastTransportStep = 0`; that block exercised the follow-injected note path, not the matched pattern-note boundary.
- **Root cause:** barBeat was treated as a local step index. At four steps per beat, 0.249 beat sits near local step 1 while the pattern note starts at step 4.
- **Prevention:** derive the transport position from the target step (`(step + small delta) / stepsPerBeat`) and set the previous step accordingly; assert the emitted note pitch before asserting its velocity.

## drumgen: mixed-type `std::max` velocity boost (2026-11-24)
- **What happened:** the standalone core probe failed to compile with `no matching function for call to 'max(uint8_t&, const int&)'` while adding continuous backbeat velocity.
- **Root cause:** `std::max` deduces one template type from both arguments; `uint8_t` velocity and an `int` boost cannot be deduced together.
- **Prevention:** cast both operands to `int` before `std::max`, then cast back; compile the standalone probe before the CMake build.

## drumgen UI: unqualified `PatternTemplate` in DPF namespace (2026-11-24)
- **What happened:** the VST3 UI build failed with `‘PatternTemplate’ does not name a type` after adding the Templates empty state.
- **Root cause:** `PatternTemplate` lives in `downspout::drumgen`, but the UI code sits inside `START_NAMESPACE_DISTRHO` and does not import that namespace.
- **Prevention:** qualify cross-namespace types (`downspout::drumgen::PatternTemplate`) and build the `-vst3` target after UI edits, not just the core tests.

## docs: exact-match replacement used stale text (2026-11-24)
- **What happened:** the `plugins-summary.md` DrumGen paragraph replacement failed because the assumed line break after "CC 24 =" was not in the file.
- **Root cause:** the replacement text was composed from an earlier read rather than the current file contents.
- **Prevention:** read the exact target lines immediately before an exact-match edit; anchor on a short, verified unique snippet.

## Template

## <plugin/area>: <short description> (YYYY-MM-DD)
- **What happened:** observable failure and how it was found.
- **Root cause:** the specific assumption or missing check.
- **Prevention:** the test, build step or design rule that prevents a repeat.
