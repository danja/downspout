# Energise progress

Exploratory first pass, built from `docs/drumloop-energise-overview.md` and
`docs/energise-plan.md`. This file is a phase-level checklist: tick items only
when a whole phase passes its exit criteria.

## Decisions on the open choices

- **Capture source:** record 1–8 s (default 4 s) from stereo inputs 1/2. No WAV
  import in this pass (would need a portable reader); a built-in synthetic Demo
  break provides a no-input path for review and tests.
- **Maximum capture length:** hard 8 s limit, enforced in the core and tests.
- **Classifier baseline:** portable rule/feature classifier only.
- **ML runtime:** none. Classifier and voice interfaces stay swappable; no
  vendored model or training data.
- **MIDI output:** offline GM-drum export on channel 10 only, queued from the
  offline job and flushed from `run()`. No real-time playable event mode.
- **Host replacement:** none. The plugin outputs the monitored captured or
  rendered buffer; A/B swaps which one is heard.

## Implementation checklist

- [x] Phase 1 — data model, parameters, processor state machine, fixtures and core tests
- [x] Phase 2 — onset detection, tempo/grid fit and reviewable event list
- [x] Phase 3 — rule classifier, quantisation and MIDI export
- [x] Phase 4 — transient/body/residual renderer, deterministic and stable
- [ ] Phase 5 — versioned state, DPF wrapper, waveform/event UI and screenshot review
- [ ] Phase 6 — release wiring, docs/catalog and full validation

Phase 5 code, state, VST3 build and a 1120x780 screenshot are complete; the
catalog image is copied provisionally but human visual acceptance is still
pending. Phase 6 wiring and docs are in place, leaving host listening and
save/reopen validation before either phase is ticked.
