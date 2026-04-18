# downspout

`downspout` is a from-scratch repository for native plugin ports of selected `flues` LV2 projects, with an initial focus on VST3-capable builds via DPF.

The immediate targets are:

- `bassgen`: a transport-aware MIDI generator
- `p-mix`: a transport-aware probabilistic audio effect

The repository is intentionally scaffolded around a shared-core pattern:

- `plugins/<plugin>/`
  Plugin-specific DSP wrapper, UI wrapper, metadata, and docs.
- `src/common/`
  Shared utilities that are safe to reuse across plugins.
- `include/downspout/`
  Shared public headers for reusable code.
- `tests/`
  Unit and host-facing regression tests.
- `third_party/`
  Vendored external dependencies once pinned and approved.

## Status

This repository currently contains planning and scaffolding only. No plugin port is implemented yet.

## Next steps

1. Vendor or pin DPF in `third_party/DPF`.
2. Extract host-agnostic logic from the `flues` LV2 targets into reusable core modules.
3. Create the first DPF VST3 builds for `bassgen` and `p-mix`.
4. Add automated tests around transport handling, state, and deterministic behavior.
