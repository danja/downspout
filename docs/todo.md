# Downspout TODO

## P-Mix

Add a slider labelled Oppose. The idea is for when other channels are busy, the P-Mix channel will be quieter and vice versa. To do this P-Mix will monitor the input level of audio channels 3 and 4 (integrated a bit) and depending on the level and the Oppose setting modify the probability of quitening or increasing the level of the main signal in channels 1 & 2.  

## Gremlin

Simplify UI — left open pending user design direction on which controls to hide/merge.

## Floozy

Floozy has no Conductor integration yet (physical modelling synth). If added, avoid hardcoding CC numbers — expose them as parameters or document they must match Conductor's panel settings (default CC 20–24).

## General

Move ./docs/todo.md to ./TODO.md and update references.

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
