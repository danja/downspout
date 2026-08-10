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

### Pending — Ground integration

Ground has no MIDI input and is a long-form structural generator (8–64 bar forms,
phrase roles, arc tension). Three tiers of Conductor integration are possible:

**Tier 1 — same pattern as BassGen/DrumGen** (easy, follow existing code)

Add MIDI input port and `Conductor Ch` parameter to Ground. Map:
- CC21 Density → `density`
- CC22 Energy → `motion` (more energy = more melodic movement)
- CC23 Mutation → `vary`
- CC24 Reset=127 → `actionNewForm` (restart whole arc at Intro/Coda boundaries)

**Tier 2 — section → arc shape** (one extra CC, unique to Ground)

- CC20 Scene (0–127) → `tension` (scaled 0.0–1.0)

Ground's `tension` controls where the form's dynamic peak sits. Mapping Conductor's
scene position (Intro=0 … Coda=127) to tension means the arrangement position
directly reshapes Ground's internal arc, so the form peaks align with the
arrangement's energy peak. BassGen and DrumGen have no equivalent.

**Tier 3 — section vocabulary → phrase role overrides** (most musically meaningful,
more design work)

Ground has a 32-slot phrase role override array and a `kParamStatusPhrase` output
parameter. Conductor's section vocabulary maps naturally to Ground's phrase roles:

| Conductor section | Scene CC | Ground phrase role |
|---|---|---|
| Intro | 0 | Statement |
| Develop | 32 | Climb |
| Break | 64 | Breakdown or Pedal |
| Reprise | 96 | Answer |
| Coda | 127 | Cadence → Release |

On each section boundary (CC20 arrives), write the role override for the current
phrase (read from `kParamStatusPhrase`) and optionally the next few phrases.

Design questions to resolve before implementing Tier 3:
- How many phrases ahead to override (1? 2? all remaining in the form?).
- What happens when Conductor changes section mid-phrase — apply immediately or
  wait for the next phrase boundary?
- Whether the DPF wrapper should track `kParamStatusPhrase` internally or rely on
  `parameterChanged` updates from the host.

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
