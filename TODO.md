# Downspout TODO

## New Instrument Plugin : Moka

Moka is implemented under `plugins/moka/` as a stereo modal hit-object
synth (xylophone, glockenspiel, woodblock, glass bowl, metal sheet, tube)
with eight controls (Instrument, Decay, Mallet, Tone, Spread, Position,
Voices defaulting to 4, Level), ten factory presets, and the shared
Cold War test-equipment look & feel with dark/light panel themes.

Remaining: host validation in REAPER.

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
