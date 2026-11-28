# Energise progress

Exploratory first pass, built from `docs/drumloop-energise-overview.md` and
`docs/energise-plan.md`. This file is a phase-level checklist: tick items only
when a whole phase passes its exit criteria.

## Decisions on the open choices

- **Capture source:** arm 1–4 whole bars (default 2) from the next host bar line
  and record stereo inputs 1/2. No WAV import in this pass (would need a portable
  reader); a built-in synthetic Demo break provides a no-input path for review.
- **Tempo:** hard-locked to the host BPM/phase from the bar-line capture start;
  the independent FFT/tempo fit is no longer used by the processor.
- **Maximum capture length:** hard 8 s limit, enforced in the core and tests;
  slow tempos reduce the effective bar count.
- **Classifier baseline:** portable rule/feature classifier only.
- **ML runtime:** none. Classifier and voice interfaces stay swappable; no
  vendored model or training data.
- **MIDI output:** GM-drum note-on/note-off pairs on channel 10 only, toggled by
  MIDI Out, refreshed after edits, and repeated with the shared audio loop (host
  BBT when locked, free-running otherwise). No real-time playable event mode.
- **Audio output:** Off (default), Pass (monitor inputs) or Render (loop the
  captured/rendered buffer); A/B swaps the Render buffer. BBT Loop locks audio and
  MIDI loop phase to host bars.

## Implementation checklist

- [x] Phase 1 — data model, parameters, processor state machine, fixtures and core tests
- [x] Phase 2 — onset detection, tempo/grid fit and reviewable event list
- [x] Phase 3 — rule classifier, quantisation and MIDI export
- [x] Phase 4 — transient/body/residual renderer, deterministic and stable
- [ ] Phase 5 — versioned state, DPF wrapper, waveform/event UI and screenshot review
- [ ] Phase 6 — release wiring, docs/catalog and full validation

Phase 5 code and state are built and tested. The BBT/output-switch revision
changed the UI to 1120x840 and invalidated the copied catalog screenshot; a fresh
capture exists at `/tmp/opencode/energise-shots/energise.png`, but human visual
acceptance is still pending. Phase 6 wiring and docs are in place, leaving host
transport/listening and save/reopen validation before either phase is ticked.
