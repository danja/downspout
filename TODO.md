# Downspout TODO

* check builds for warning messages. Any that can be resolved in the local codebase should be 

## Refactoring

promotion of the shared OnePoleLowpass/DCBlocker/BiquadFilter duplicates into
  include/downspout/dsp/. That refactor is worth doing but is a separate change and
  CLAUDE.md requires approval before touching shared headers.
  HANDLE WITH CARE
  
## Look and Feel

It is desirable to give all the downspout plugins a consistent look as well as /home/danny/github/valis and /home/danny/github/transmission

These use different frameworks but presumably a small custom component lib could be used with both DPF and JUCE. Investigate how this might be done.

The Look & Feel should have both a light and dark theme. The light theme should resemble Cold War era electronic test equipment, quite minimal and brutal but intuitive. The dark theme will simply be the inverse.

## Evaluate Manually in Reaper

* ambo
* arpgen
* conductor
* drift
* harmonic-atlas
* mnemosyne
* magneto
* moka
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
