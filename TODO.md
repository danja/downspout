# Downspout TODO

* check builds for warning messages. Any that can be resolved in the local codebase should be 

## Refactoring

promotion of the shared OnePoleLowpass/DCBlocker/BiquadFilter duplicates into
  include/downspout/dsp/. That refactor is worth doing but is a separate change and
  CLAUDE.md requires approval before touching shared headers.
  HANDLE WITH CARE
  
## New Plugins (from DIM)

* [ ] Treatment (`/farelo/task/t61f30fb095c3`) - new plugin. Simulates by physical
  modelling residual treatment acoustic panel. Give it a handful of parameters
  (also controllable via midi cc) and include a Randomise button
* [ ] combined vocoder and ring mod plugin (`/farelo/task/t12a6e84467ce`) - taking
  modulation on audio channels 3 & 4. Should accept midi cc for changing
  parameters. Same look & feel as magneto.

## Scales

* [ ] add rhumba, samba and township jive (`/farelo/task/td4792ccd13b9`)
  - see `docs/scales.md`; append-only, canonical ordering and naming rules apply

## Evaluate Manually in Reaper

* helterskelter
* ghost
* spliff
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
* worms

## Recurring - check periodically

* remove tasks that have been done from this file
* check MISTAKES.md for any systematic problems, promote info on these to CLAUDE.md
* if an issue in MISTAKES.md has been fully resolved, remove it from the file
* for new material, check test coverage
* ensure README.md and docs are up-to-date
