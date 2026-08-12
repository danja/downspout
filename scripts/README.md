# scripts/

Reusable automation for building, packaging, and composing with Downspout
plugins.  Scripts that produce REAPER projects write their output to
`artifacts/reaper/`.

---

## REAPER composition scripts

### `create-generative-suite-reaper-project.lua`

Builds a broad showcase of the Downspout generative suite: Conductor driving
form, Harmonic Atlas, Polymeter → DrumKit, Mnemosyne motif memory, Drift pad,
and Mosaic texture, all wired through Resonance Garden and Orbit.  Intended as
an orientation project for the full plugin roster.

Run from REAPER → Actions → Load ReaScript.  Output:
`artifacts/reaper/generative-workstation-lab.RPP`

### `mosaic-showcase.lua`

48-bar piece at 90 BPM in D minor that puts **Mosaic** centre-stage as a
grain/collage instrument.  Uses the zero-crossing onset snap introduced in this
build so short attack times (~5 ms) work without clicks.

**Arrangement in four sections:**

| Section | Bars | Content |
|---------|------|---------|
| A | 1–12 | Mosaic sparse grains (2-beat grid) through P-Mix rhythmic gate |
| B | 13–24 | Orchid texture enters: Canticle chords freeze-held and MIDI pitch-shifted |
| C | 25–36 | Full density — DrumGen/DrumKit and BassGen/Basilico join |
| D | 37–48 | Fade: Mosaic returns to sparse grid, low velocities |

Shared Ambo reverb bus receives sends from Mosaic and Orchid.  Guardian
limiter on the master.

**Setup:** load 1–4 WAV files into Mosaic's sample slots before pressing Play.
Set BassGen root = D, scale = Minor; choose Basilico Dub model.

Output: `artifacts/reaper/mosaic-showcase.RPP`

### `reaper-modal-jazz-arrangement.py`

Emits a REAPER-MCP MIDI arrangement recipe as JSON (relative-time note dicts
compatible with `create_midi_clip`).  Produces an original modal-jazz
vocabulary rather than transcribing any existing piece.

Run standalone; pipe output to the REAPER MCP bridge.

### `add-reaper-organ-accompaniment.py`
### `add-reaper-volume-envelopes.py`
### `extend-reaper-fire-trio.py`
### `rephrase-reaper-clarinet.py`
### `revoice-reaper-clarinet.py`

Project-specific editing scripts for the fire-trio and modal-jazz-suite sessions
in `artifacts/reaper/`.  Each script targets a named RPP file and applies a
single focused transformation (reharmonise, add envelopes, swap instrument,
extend length, etc.).

---

## Build and release scripts

### `package-release.sh`

Packages built VST3 bundles from the CMake install tree into a versioned
release archive.  Called by the CI release workflow; can also be run locally
after `cmake --install`.

### `package-built-bundles.sh`

Lighter-weight variant that packages whatever bundles are present in the local
build directory without requiring a full install step.

### `capture-plugin-screenshots.sh`

Launches each plugin in the DPF standalone runner, captures a screenshot with
`scrot`, and writes it to `docs/pages/assets/plugins/<name>.png`.  Requires a
display (real or virtual).  Called during the new-plugin completion checklist.
