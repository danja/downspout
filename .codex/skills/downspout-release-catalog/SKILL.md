---
name: downspout-release-catalog
description: Maintain downspout release packaging, install scripts, GitHub Actions release assets, screenshots, and GitHub Pages plugin catalog metadata. Use when Codex changes plugin bundle lists, adds/removes public plugins, edits docs/pages products, or updates screenshot/release workflows.
---

# Downspout Release Catalog

Use this skill when bundle lists, release packaging, install behavior, screenshots, or Pages product metadata might be affected.

## Release/package checklist

1. Compare plugin-local CMake install declarations with `scripts/package-release.sh` expected bundles.
2. Keep `install.sh` build options aligned with root `CMakeLists.txt` plugin options.
3. Keep release docs aligned with actual package contents.
4. Update `.github/workflows/release.yml` when platform artifact or bundle assumptions change.
5. Run syntax checks for changed shell scripts and workflow checks where available.

## Pages/screenshot checklist

1. Ensure every public plugin has `docs/pages/_products/<plugin>.md` front matter.
2. Ensure each product references an existing `docs/pages/assets/plugins/<plugin>.png` screenshot.
3. Ensure `scripts/capture-plugin-screenshots.sh --list` includes the plugin when screenshots are expected.
4. Regenerate screenshots when UI appearance changes and the required local tools are available.
5. Open each new or materially changed screenshot at full resolution and review
   it as a first-time user. Require clear purpose, workflow, grouping, labels,
   values, units, routing/status feedback, useful defaults, and readable,
   unclipped controls. Revise and recapture when it fails.
6. Update `docs/screenshots.md` and `docs/pages/README.md` only when the process changes.

Read [references/release-catalog.md](references/release-catalog.md) for commands and expected file relationships.
