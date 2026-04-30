# release

Release packaging is intentionally narrow for now: Linux x86_64 VST3 bundles
for the currently enabled plugins.

## Current public artifact

Tagged releases publish:

```text
downspout-<version>-linux-x86_64-vst3.zip
├── bassgen.vst3/
├── p_mix.vst3/
├── e_mix.vst3/
├── rift.vst3/
├── drumgen.vst3/
├── cadence.vst3/
├── gremlin.vst3/
├── gremlin_driver.vst3/
├── ground.vst3/
├── LICENSE
└── README.md
```

A matching `.sha256` file is published alongside the zip.

## Local release package

To create the same package locally:

```bash
DOWNSPOUT_VERSION=0.1.0 bash scripts/package-release.sh
```

Useful overrides:

- `DOWNSPOUT_BUILD_DIR`
  CMake build directory. Default: `build/release`.
- `DOWNSPOUT_DIST_DIR`
  Directory for zip and checksum output. Default: `dist`.
- `DOWNSPOUT_STAGING_DIR`
  Temporary install root. Must be empty if provided.
- `DOWNSPOUT_PACKAGE_DIR`
  Temporary package root. Must be empty if provided.
- `DOWNSPOUT_RELEASE_PLATFORM`
  Platform string used in the artifact name. Default is detected from
  `uname`.
- `DOWNSPOUT_STRIP`
  Set to `0` to skip Linux shared-object stripping.
- `DOWNSPOUT_RUN_TESTS`
  Set to `0` to skip `ctest`.

## GitHub Actions

Two workflows are defined:

- `.github/workflows/ci.yml`
  Builds, tests, installs, packages, and stores the package as a workflow
  artifact for branch pushes, pull requests, and manual runs.
- `.github/workflows/release.yml`
  Runs for tags matching `v*`, builds the package, and creates a GitHub
  Release with the zip and checksum assets.

The release workflow derives the artifact version from the tag by removing the
leading `v`. For example, tag `v0.1.0` produces
`downspout-0.1.0-linux-x86_64-vst3.zip`.

## Publishing From Main

If you are releasing from `main`, the usual flow is:

1. Make sure the release-worthy changes are already committed on `main`.
2. Push `main`.
3. Create a new version tag.
4. Push the tag.
5. Watch the `Release` workflow on GitHub Actions.

Example for `v0.1.1`:

```bash
git checkout main
git pull
git push origin main

git tag v0.1.1
git push origin v0.1.1
```

Notes:

- Use a new tag each time. Do not reuse an existing release tag such as
  `v0.1.0`.
- The tag must match `v*` or the release workflow will not run.
- The GitHub Release is created by the workflow, not by `git push` alone.
- If the workflow fails, fix the issue and cut a new tag such as `v0.1.2`.

## Assumptions

- Public automation currently targets `ubuntu-24.04` only.
- The VST3 bundles are dynamically linked against normal Linux desktop/plugin
  runtime libraries used by the DPF UI path, including X11/OpenGL-related
  libraries.
- macOS and Windows artifacts are intentionally deferred until those builds and
  host-validation paths are proven.
- Host validation remains required before treating a tag as a public-quality
  release.
