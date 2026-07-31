---
name: downspout-add-plugin
description: Add a new downspout plugin end to end. Use when Codex is asked to scaffold or port a plugin under the plugins directory and must keep CMake, install, release packaging, docs, screenshots, and GitHub Pages catalog entries synchronized.
---

# Downspout Add Plugin

Use this skill for creating a new plugin or bringing an incomplete plugin into the normal build/release/catalog flow.

## First decisions

1. Identify whether this is an LV2 port or an original plugin.
2. Identify plugin type: audio effect, MIDI generator/effect, instrument, Launchpad/control plugin, or AI-backed plugin.
3. Choose a reference plugin with the same shape before writing new code.
4. Keep the first implementation minimal: portable core, deterministic tests, thin DPF wrapper, then UI.

## Required synchronization

When adding a plugin, update all of these in the same change unless the user explicitly scopes the work smaller:

- plugin-local `CMakeLists.txt` with core library, optional DPF target, install rule, and core tests;
- root `CMakeLists.txt` build option and `add_subdirectory` gate;
- `install.sh` build option list;
- `scripts/package-release.sh` build option and expected bundle list;
- `.github/workflows/release.yml` if release artifact bundle handling is enumerated there;
- `README.md`, `docs/install.md`, `docs/release.md`, `docs/architecture.md`, and `docs/plan.md` where bundle lists or project status change;
- `docs/pages/_products/<plugin>.md` with front matter;
- `scripts/capture-plugin-screenshots.sh` plugin list and screenshot capture target;
- generated `docs/pages/assets/plugins/<plugin>.png` when UI screenshots are in scope;
- `docs/screenshots.md` and `docs/pages/README.md` if screenshot/catalog processes change.

## Implementation shape

Build the plugin around a portable core first. Do not make the DPF wrapper own the architecture. Add explicit docs for LV2-to-DPF/VST3 mappings, especially transport, state, MIDI ports, and UI behavior.

Read [references/new-plugin-checklist.md](references/new-plugin-checklist.md) for concrete file patterns and validation.

## Visual acceptance gate

After the real UI target renders, capture its screenshot and inspect the image
at full resolution before declaring the plugin complete. Judge it as a
first-time user: purpose, signal flow, primary workflow, grouping, labels,
values, units, modes, routing, and live status must be understandable; defaults
must look useful; and nothing may be clipped, crowded, ambiguous, or illegible.
Fix the UI and repeat capture plus inspection until it passes. If the local
environment cannot capture or inspect images, report visual acceptance as
explicitly pending.
