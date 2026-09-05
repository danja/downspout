# Downspout TODO

## Campione

Implement REX2 support using VelociLoops (`~/github/VelociLoops`). Feasibility assessed: viable, ~100 lines of wrapper in `campione_sample_loader.cpp`, add `loadRex2Zones()` that iterates slices via `vl_get_slice_info()` / `vl_decode_slice()` and populates `SampleZone` entries. Link `velociloops_static` in CMakeLists.txt.

## Evaluate Manually in Reaper

* ambo
* arpgen
* conductor
* drift
* harmonic-atlas
* mnemosyne
* mosaic
* oracle
* orbit
* polymeter
* resonance-garden
* tuney-vst

## Recurring - check periodically

* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
