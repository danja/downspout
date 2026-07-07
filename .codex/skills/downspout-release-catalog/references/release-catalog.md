# Release And Catalog Relationships

## Files that must agree

Build inclusion:

- root `CMakeLists.txt` `DOWNSPOUT_BUILD_*` options and `add_subdirectory` gates
- plugin-local `CMakeLists.txt` install declarations
- `install.sh` option list
- `scripts/package-release.sh` option list and expected bundle names
- `.github/workflows/release.yml` when bundle/platform handling is explicit

Public docs/catalog:

- `README.md`
- `docs/install.md`
- `docs/release.md`
- `docs/architecture.md`
- `docs/plan.md`
- `docs/pages/_products/<plugin>.md`
- `docs/pages/assets/plugins/<plugin>.png`
- `scripts/capture-plugin-screenshots.sh`
- `docs/screenshots.md`
- `docs/pages/README.md`

## Useful checks

Find installed bundles declared by plugin CMake:

```bash
rg -n "install\(DIRECTORY .*\.vst3" plugins/*/CMakeLists.txt
```

Find release script bundle expectations:

```bash
rg -n "expected|vst3|DOWNSPOUT_BUILD_" scripts/package-release.sh
```

Check product pages and screenshots:

```bash
find docs/pages/_products -maxdepth 1 -name "*.md" -print | sort
find docs/pages/assets/plugins -maxdepth 1 -name "*.png" -print | sort
scripts/capture-plugin-screenshots.sh --list
```

Run syntax checks after shell edits:

```bash
bash -n install.sh
bash -n scripts/package-release.sh
bash -n scripts/capture-plugin-screenshots.sh
```

If workflows changed and `actionlint` is available:

```bash
actionlint
```

## Screenshot generation

Use:

```bash
scripts/capture-plugin-screenshots.sh
```

Selected plugins:

```bash
scripts/capture-plugin-screenshots.sh rift p-mix
```

Reuse an existing screenshot build only when it was configured with `DOWNSPOUT_BUILD_SCREENSHOT_APPS=ON`:

```bash
scripts/capture-plugin-screenshots.sh --skip-build
```

The script may need JACK, Xvfb, xdotool or xwininfo/xprop, and ImageMagick. If tools are unavailable, state exactly what could not be run and leave the catalog file updates consistent.

## Release package command

Local package:

```bash
DOWNSPOUT_VERSION=0.1.0 bash scripts/package-release.sh
```

Useful overrides: `DOWNSPOUT_BUILD_DIR`, `DOWNSPOUT_DIST_DIR`, `DOWNSPOUT_STAGING_DIR`, `DOWNSPOUT_PACKAGE_DIR`, `DOWNSPOUT_RELEASE_PLATFORM`, `DOWNSPOUT_STRIP`, `DOWNSPOUT_RUN_TESTS`.
