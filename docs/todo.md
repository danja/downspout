# Downspout TODO

## Conductor → generator integration

### Done

- **BassGen**: MIDI input port already present. Added `Conductor Ch` parameter (0=off,
  1–16). When active, Conductor CCs on that channel map to:
  - CC21 Density → `density`
  - CC22 Energy → `accent`
  - CC23 Mutation → `vary`
  - CC24 Reset=127 → `actionNew`
- **DrumGen**: Added MIDI input port and `Conductor Ch` parameter. CC mapping:
  - CC21 Density → `density`
  - CC22 Energy → `variation`
  - CC23 Mutation → `vary`
  - CC24 Reset=127 → `actionNew`
- Both plugins have a **Conductor Ch** dropdown in the UI (right panel, grouped under
  "Conductor" section in BassGen). See `docs/midi-mapping.md` for full protocol.

### Done — Ground integration

Ground gained a MIDI input port and `Conductor Ch` parameter (0=off, 1–16).
When active, Conductor CCs on that channel map to:

**Tier 1** — same CC pattern as BassGen/DrumGen:
- CC21 Density → `density`
- CC22 Energy → `motion`
- CC23 Mutation → `vary`
- CC24 Reset=127 → `actionNewForm`

**Tier 2** — section → arc shape (unique to Ground):
- CC20 Scene (0–127) → `tension` (scaled 0.0–1.0)

**Tier 3** — section vocabulary → phrase role override for current phrase:
- CC20 0–16 → Statement, 17–48 → Climb, 49–80 → Breakdown, 81–112 → Answer, 113–127 → Cadence
- Writes the override immediately for the phrase currently playing (statusPhrase_)
- Future work: override ahead by 1–2 phrases, or wait for phrase boundary

See `docs/midi-mapping.md` for the full protocol and mapping tables.

### Pending — configurable CC numbers

All three generators (BassGen, DrumGen, and the planned Ground integration) currently
hardcode Conductor's default CC numbers (20–24). If the user changes Conductor's CC
assignments, the generators will not follow. Consider exposing CC number parameters
on the receiver side, or adding a note to the UI that CC numbers must match
Conductor's settings.

### Pending — Conductor CC channel collision note

The BassGen "MIDI Follow" system (Note On response for drum triggering) and the
"Conductor" system (CC response) operate on different MIDI message types, so they
can coexist on the same channel. However, the UI should make this clearer — consider
a tooltip or panel note explaining that Follow Ch and Conductor Ch serve different
purposes and can differ.
