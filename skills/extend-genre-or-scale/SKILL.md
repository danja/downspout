---
name: extend-genre-or-scale
description: Add a genre, scale, mode or other persisted enum value to an existing Downspout plugin (e.g. a new Bassgen genre) without breaking saved projects. Use for any append to GenreId, ScaleId, engine lists or note maps.
---

# Extend a persisted enum

Enums are stored as integers in host project state. **Append only, before `count`.** Never insert or reorder. For scales, `docs/scales.md` is the primary reference (canonical order, per-plugin coverage, naming); read it first and update its tables.

## Steps (Bassgen genre as the worked example; Descending = 12, Descend/bar = 13)
1. Core enum (`include/<plugin>_core_types.hpp`): add before `count`.
2. Behaviour: find every `switch`/comparison on the enum. Search the new neighbour's name (`grep -n "GenreId::moroder"`), because genres affect density bias, hold/duration, rhythm reinforcement, note choice, and regeneration paths. If two values share behaviour, add a small `isXxx()` helper rather than duplicating branches.
3. Regeneration paths: rhythm-only and notes-only regeneration and `partialNoteMutation` must keep the invariant the genre promises. Anything that copies notes by event index breaks if notes depend on the event count.
4. Clamp/serialization: `clampControls` uses `count - 1`; the DPF parameter max is derived from `count`. Check both still hold.
5. UI: add the label to the names array and bump the item count in the selector (`{kParamGenre, "Genre", kGenreNames, N}`). Keep labels short enough for the segment width; check the screenshot if the control is visible.
6. Tests (see `plugin-core-tests`): pin the new integer and `count`; update the "highest value" clamp test; test the genre's defining property across all scales/densities/subdivisions and several seeds; test rhythm-only and notes-only regeneration and partial mutation.
7. Docs, same change: plugin `README.md`, `profile.ttl` (genre list), `docs/pages/_products/<plugin>.md`, `docs/pages/plugins-summary.md`, `docs/summary.md` if user-facing.
8. Build `<plugin>-vst3`, run tests, reinstall with `install.sh` if the user wants to try it, and say whether it was heard.

## Scales
A new scale also needs: the canonical table and per-plugin table in `docs/scales.md`, an independent expected interval set in the test (diff against existing tables elsewhere in the repo first), and display name conventions from that doc. Generate docs from the source enum, not from the reference it was derived from.
