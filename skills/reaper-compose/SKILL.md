---
name: reaper-compose
description: Compose, arrange, mix, save, and verify original music in a connected REAPER session through REAPER MCP. Use for requests to create songs, instrumentals, MIDI arrangements, plugin-based productions, or reusable DAW composition recipes, especially when Downspout VST3 instruments and effects are available.
---

# Compose in REAPER

## Preserve the session

1. Inspect the current project and tracks before making changes.
2. Create a new named project for a new composition; never overwrite an unrelated project.
3. Save early to a user-approved durable path and save again after material changes.

## Design before editing

1. Translate artist-style requests into high-level musical traits. Create original melodies, harmony, rhythms, and form; do not transcribe or imitate a specific recording.
2. State the tempo, meter, ensemble, tonal center, form, and section lengths.
3. Prefer a short complete form over a long undifferentiated loop.

## Build the arrangement

1. Create clearly named tracks and add instruments before MIDI.
2. Use `create_midi_clip` for batches. Keep event times relative to the item start and use MIDI channel 9 for drums.
3. Separate lead, harmony, bass, and percussion so they remain independently editable.
4. Add expressive variation through velocity, register, density, rests, and motivic development.
5. When working in Downspout, read `docs/summary.md` and choose plugins from the documented capability map. Put MIDI processors before instruments.

## Mix conservatively

1. Establish balance and modest pan before adding effects.
2. Use sends for shared ambience. Start sends quiet and avoid dense feedback effects unless requested.
3. Treat MCP success fields as authoritative. If an FX helper returns an error, report it and use a safe manual alternative or leave headroom.

## Persist reusable work

1. Save reusable generators and recipes in the project workspace, not `/tmp`.
2. Prefer a parameterized script that emits `create_midi_clip`-compatible JSON for algorithmic arrangements.
3. Validate scripts by running them and checking event counts and musical duration.

## Verify and hand off

1. Save the `.RPP` before rendering.
2. Render to a durable local artifact path when the bridge permits it.
3. Check clipping and loudness after a successful render. Do not claim an audio artifact exists if rendering timed out.
4. Report project path, render path if created, arrangement summary, verification, and any bridge limitations.
