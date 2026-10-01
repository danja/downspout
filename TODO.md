# Downspout TODO

* check builds for warning messages. Any that can be resolved in the local codebase should be 

## Refactoring

promotion of the shared OnePoleLowpass/DCBlocker/BiquadFilter duplicates into
  include/downspout/dsp/. That refactor is worth doing but is a separate change and
  CLAUDE.md requires approval before touching shared headers.
  HANDLE WITH CARE

## keyframe display and density scaling

* Density meter saturated by construction (fixed 2026-10-02): `log10(rate)/4.0`
  pegged the bar at 10,000 kf/s against a 24,000 parameter max, and the host
  clamped the output parameter on top, so the meter read stuck and the tick
  strip froze. Fixed by reporting density as the paper's M/N (keyframes per
  input sample), bounded to [0,1] and sample-rate independent; kf/s text was
  dropped because the UI has no sample rate to scale it by.
* Density smoothing was per-block, not per-second (fixed 2026-10-02): a fixed
  0.02 coefficient converged 4x faster at 128-sample blocks than at 512. Now a
  time-based coefficient (tau 0.15 s). The block-size-invariance test covers
  audio only; test 14c pins the status path.
* Leash diagram was static at unity rates because drift is genuinely ~0 there.
  Added a splice halo around the playhead dot so the panel reads live during
  crossfades. If it still reads dead at defaults, the diagram needs a rethink,
  not another lamp.
  
## New Plugins (from DIM)

* [x] Treatment (`/farelo/task/t61f30fb095c3`) - DONE. Mass-air-mass panel
  absorber derived from real cavity / air gap / facing mass / flow resistivity,
  with the impedance-match sweet spot, a seeded Randomise that preserves Amount
  and Bypass, five Drift CC lanes, and a live absorption plot on the panel.
  See `plugins/treatment/README.md` and `plugins/treatment/docs/design.md`.
* [ ] combined vocoder and ring mod plugin (`/farelo/task/t12a6e84467ce`) - taking
  modulation on audio channels 3 & 4. Should accept midi cc for changing
  parameters. Same look & feel as magneto.

## Scales

* [ ] add rhumba, samba and township jive (`/farelo/task/td4792ccd13b9`)
  - see `docs/scales.md`; append-only, canonical ordering and naming rules apply

## Evaluate Manually in Reaper

* helterskelter
* keyframe
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
