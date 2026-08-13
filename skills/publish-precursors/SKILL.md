---
name: publish-precursors
description: Run all web-publishing precursors for one or more Downspout plugins. Checks profile.ttl accuracy, verifies screenshot script registration, captures a fresh screenshot, inspects the PNG, and validates the GitHub Pages product front matter. Stops before any git or deploy step — those are the user's responsibility.
---

# Publish precursors for a Downspout plugin

Run this skill after any structural or UI change to a plugin before the change
is considered complete. Pass one or more plugin slugs (e.g. `floozy`, `orchid`).
When no slugs are given, prompt the user to supply them.

## 1. Profile check

For each plugin slug:

- Read `plugins/<slug>/profile.ttl`.
- Read the plugin's params header (typically `plugins/<slug>/include/<slug>_params.hpp`
  or equivalent) and the DPF plugin source to identify current parameter count,
  routing (audio/MIDI in/out), and any CC mappings.
- Compare against the profile's `rdfs:comment`, `trn:accepts`, `trn:produces`,
  `trn:requires`, and any `trn:ccMapping` blocks.
- If the profile is stale, update it in place and report what changed.
  Keep comments factual and concise; do not pad with marketing language.

## 2. Screenshot script registration

- Run `scripts/capture-plugin-screenshots.sh --list` and confirm the slug
  appears in the output.
- If missing, add `"<slug>:<exe_name>"` to the `plugins=(...)` array in
  `scripts/capture-plugin-screenshots.sh` at a position consistent with the
  plugin's role (generators, then effects, then instruments, then controllers).
  The exe name is the slug with hyphens replaced by underscores.

## 3. Screenshot capture

Run from the repository root:

```bash
scripts/capture-plugin-screenshots.sh --skip-build <slug>
```

If the screenshot build does not yet exist, omit `--skip-build`.

- Treat any non-zero exit code as a failure. Report the last 30 lines of output
  and stop — do not claim a screenshot was captured.
- On success, confirm the PNG exists at `docs/pages/assets/plugins/<slug>.png`.

## 4. Screenshot review

Read the captured PNG and inspect it as a first-time user:

- Plugin purpose and signal flow are apparent without reading source code.
- Primary controls are labelled, grouped, and show sensible default values.
- No text or controls are clipped, crowded, or illegibly small.
- Live status, mode indicators, and routing contracts are visible where relevant.

If the UI fails any of these checks, describe the specific problem and stop.
Do not mark the step complete until the screenshot is acceptable. After any
UI fix, re-run step 3 and re-inspect.

## 5. Pages product file check

- Read `docs/pages/_products/<slug>.md`.
- Verify all required front matter fields are present and accurate:
  `title`, `order`, `bundle`, `kind`, `role`, `screenshot`, `summary`.
- `screenshot` must be `/assets/plugins/<slug>.png`.
- `summary` must reflect the current plugin behaviour (update if stale).
- If the file does not exist, create it following the pattern of an existing
  product file and leave a clear note that `order` needs manual placement.

## 6. Report

Produce a short table:

| Step | Plugin | Result |
|------|--------|--------|
| profile.ttl | <slug> | updated / already current |
| screenshot script | <slug> | present / added |
| screenshot capture | <slug> | OK / FAILED |
| screenshot review | <slug> | passed / issues noted |
| pages product | <slug> | OK / updated / created |

Note any items that need follow-up (UI fixes, manual order placement, etc.).
Do not mention git — committing is the user's responsibility.
