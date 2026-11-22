# Mistakes log

New, unresolved entries only. Resolved lessons are promoted to `AGENTS.md` and removed from here.

Format: what happened, root cause, prevention.

## Open

## fontana: test used a harness-only scratch path (2026-11-22)
- **What happened:** `downspout_fontana_core_tests` aborted at `testWavLoadingAndPeaks` (`assert(loaded.error.empty())`) on a machine where `/tmp/opencode` did not exist. `writeBytes` silently failed to create the WAV, so `loadSampleSource` returned "Could not open WAV file".
- **Root cause:** `scratchPath()` in `fontana_core_tests.cpp` hardcoded `/tmp/opencode`, a path that only exists inside the OpenCode harness, and `writeBytes` did not check that the file opened.
- **Prevention:** use `std::filesystem::temp_directory_path()` (with `create_directories`) for test scratch files, as the NAM and Rift tests do; assert that the ofstream is good before writing. Fixed by deriving the scratch dir from `temp_directory_path()` and adding `assert(stream)` in `writeBytes`.

## Template

## <plugin/area>: <short description> (YYYY-MM-DD)
- **What happened:** observable failure and how it was found.
- **Root cause:** the specific assumption or missing check.
- **Prevention:** the test, build step or design rule that prevents a repeat.
