---
name: debug-reaper-scan
description: Diagnose a Downspout VST3 that Reaper does not list or shows as failed. Use when a plugin is built but invisible, shows zero-byte binaries, or Reaper's scan cache is stale.
---

# Plugin not recognised by Reaper

Reaper caches failed scans in `~/.config/REAPER/reaper-vstplugins64.ini`; a plugin that failed once stays hidden until its entry is cleared or the file changes.

## Procedure
1. `scripts/inspect-plugin.sh <plugin>` (add `--fix` to clear the stale cache entry and touch the `.so`). Read every FAIL/WARN.
2. Check the installed binary: `~/.vst3/<plugin>.vst3/Contents/x86_64-linux/<plugin>.so` must be non-zero size. Zero-byte files meant a copy ran before the build finished or from a stub; `install.sh` now exits 1 on this.
3. Confirm the bundle was built with the real target: `cmake --build build --target <plugin>-vst3` (plain `<plugin>` builds a stub UI library) and that the plugin's `DOWNSPOUT_BUILD_<NAME>` is ON in `install.sh`.
4. Metadata in `src/dpf/DistrhoPluginInfo.h`: unique 4-char plugin id (compare with `grep -rn DISTRHO_PLUGIN_UNIQUE_ID plugins/*/src/dpf`), name, category, input/output counts, `IS_SYNTH`/MIDI flags, `HAS_UI`.
5. Ask the user to rescan in Reaper (Preferences > Plug-ins > VST > Clear cache / re-scan). Do not edit the Reaper config by hand beyond what `--fix` does.
6. If still hidden, run the bundle through the bundle-list checks: `scripts/check-bundle-lists.sh`.

## Known noise
`inspect-plugin.sh` reports "NUM_INPUTS=0 but IS_SYNTH not set" for MIDI generators (bassgen, drumgen, melgen, ground). It predates recent work and has not been shown to stop Reaper loading them; do not "fix" it blindly.

## State problems that look like load problems
Run `scripts/check-plugin-state.sh`. `WANT_STATE` without `WANT_FULL_STATE` makes the host save defaults into every project. State is for non-parameter data only; DPF already saves parameters.

Record a new class of failure in `MISTAKES.md`.
