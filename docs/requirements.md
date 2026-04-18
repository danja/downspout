# downspout requirements

## Scope

This document defines the baseline engineering requirements for the initial `downspout` phase.

## Product requirements

- The repository must support multiple plugins with shared infrastructure.
- The first supported plugin category is native DAW plugins with VST3 as the initial practical target.
- Initial plugin ports are `bassgen` and `p-mix`.
- Ports should preserve the audible and behavioral character of the source LV2 plugins before adding new features.

## Architecture requirements

- Shared logic must live outside plugin-format wrappers.
- Plugin wrappers must remain thin and format-aware.
- Transport handling must be abstracted behind a small internal interface or data structure.
- State serialization must be explicit and versionable.
- Parameter definitions must have a single source of truth per plugin.

## Build requirements

- The top-level build must configure even if a plugin is only partially implemented.
- Third-party code must live under `third_party/`.
- DPF integration must be pinned and documented before first real build integration.

## Testing requirements

- Deterministic core logic must have automated tests.
- Transport-sensitive behavior must have regression coverage for:
  - stopped transport
  - transport start
  - rewind
  - loop wrap
  - tempo change
  - bar-length change where relevant
- Stateful plugins must have save/restore tests for representative data.

## Documentation requirements

- Root docs should describe project-wide rules and direction only.
- Plugin-specific porting notes belong under `plugins/<plugin>/docs/`.
- If a port intentionally diverges from the source LV2 behavior, that divergence must be documented.
