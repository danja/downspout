---
name: render-midi
description: Insert Midiscribe capture nodes into the current Transmission project, arm them, let the project run, then write .mid files for each MIDI-generating chain. Produces one MIDI file per instrument voice.
---

# Render a Transmission project to MIDI

Use this skill when the user says something like "render `<project>` to MIDI" or "capture MIDI from the current project". It works for autonomous generator projects (Conductor, DrumGen, Ground, MelGen, etc.) where there are no arrangement clips to export directly.

The skill inserts a Midiscribe VST3 pass-through capture node after each MIDI generator output, guides the user through a capture run, then reports where the files landed. Do not commit, push, or save the project unless the user asks.

---

## 1. Check prerequisites

- Confirm the Midiscribe VST3 bundle is installed: check that `/home/danny/.vst3/midiscribe.vst3` exists.
  If missing, tell the user to build and install it (`install.sh`) and stop.
- Call `transmission_status`. If no project is open, ask the user to open one and stop.
- Call `node_list` and note the current revision.

---

## 2. Identify capture points

Walk the graph connections looking for MIDI edges where:
- the **source** node has `midiOutputs > 0`, AND
- the **target** node has `midiOutputs == 0` (a terminal instrument — it consumes MIDI but produces none)

These are the chains worth capturing (generator → instrument). Typical examples:
- `melgen → syrinx` (melody)
- `drumgen → drumkit` (percussion)
- `ground → basilico` (bass)
- `harmonic-atlas → canticle` (harmony/pads) — capture if useful

Ignore connections where both sides are generators or pass-through processors.

For each capture point, assign:
- a **Midiscribe node ID**: `midiscribe-<target-node-id>` (e.g. `midiscribe-syrinx`)
- an **export path**: `/tmp/<projectId>-<target-node-id>.mid`
  where `projectId` is the last segment of the project graph ID (e.g. `phosphene-drift`)

---

## 3. Insert Midiscribe nodes

For each capture point, using `graph_apply_changes` (audio must be stopped):

**a. Remove** the existing direct connection from generator to instrument.

**b. Add** a new Midiscribe node:
```json
{
  "id": "midiscribe-<target>",
  "type": "http://purl.org/stuff/transmissions/VST3Plugin",
  "label": "Midiscribe (<target>)",
  "ports": { "midiInputs": 1, "midiOutputs": 1 },
  "settings": { "pluginPath": "/home/danny/.vst3/midiscribe.vst3" }
}
```

**c. Add** two connections:
- generator → `midiscribe-<target>` (midi, fromPort 0, toPort 0)
- `midiscribe-<target>` → instrument (midi, fromPort 0, toPort 0)

Batch all operations for all capture points into a single `graph_apply_changes` call where possible, or chain calls passing the updated revision each time.

---

## 4. Instruct the user

Tell the user to do the following in the Transmission UI, **in order**:

1. Open each inserted Midiscribe in its plugin editor.
2. Set **Export Path** to the path assigned in step 2 (e.g. `/tmp/phosphene-drift-syrinx.mid`).
   Each Midiscribe must have a **unique path** — they all default to `/tmp/midiscribe.mid` and will overwrite each other if not changed.
3. Set **Armed** to ON for each Midiscribe.
4. Set **Capture Beats** to the desired capture length (default 16 beats; 32 or 64 recommended for long-form pieces).
5. Start the transport and let it run for at least the capture length.
6. When ready, trigger **Write** on each Midiscribe (set the Write parameter to 1).
7. Stop the transport.

---

## 5. Confirm output

After the user signals they have written the files, check each expected path exists:

```bash
ls -lh /tmp/<projectId>-*.mid
```

Report the file list with sizes. If any are missing, ask the user whether the export path was set correctly and the Write trigger was pressed.

---

## 6. Optional — merge to multi-track MIDI

If the user wants a single combined `.mid` file, note that the `arrangement_render_midi`
MCP tool can merge arrangement clips, but since these files were captured by Midiscribe they
are standalone MIDI files. Use a tool such as `midomerge` or import all files as separate
tracks in a DAW to combine them.

---

## Notes

- Midiscribe is pass-through: inserting it does not change the sound.
- The modified project graph is unsaved unless the user explicitly saves it. The Midiscribe nodes can be removed afterwards via `node_remove` if the user wants to restore the original topology.
- If the project has no MIDI generator→instrument chains (all arrangement-clip driven), use `arrangement_render_midi` instead — this skill is not needed.
