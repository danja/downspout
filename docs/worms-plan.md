ToneWorm will be a plugin with a similar purpose to bassgen, creating a series of midi notes in time with the DAW's transport. 
The generation algorithm will use Paterson's Worms to navigate a Tonnetz layout of notes. Rules may be entered manually, generated at random, and subsequently mutated.  
ToneWorm should be receptive to signals from plugins like Conductor and may be used to influence the behaviour of plugins like Melgen.

---

## Implementation Status

### Phase 1 — Portable Core ✓ COMPLETE
- `plugins/worms/include/worms_core_types.hpp` — Controls, NoteEvent, PatternState, TransportSnapshot, ScheduledMidiEvent
- `plugins/worms/include/worms_engine.hpp` — EngineState, BlockResult, processBlock()
- `plugins/worms/src/worms_tonnetz.cpp/hpp` — Tonnetz lattice math (pitchClass, step, midiNote, quantize)
- `plugins/worms/src/worms_pattern.cpp/hpp` — Pattern generation, randomize/mutate, findActiveEvent
- `plugins/worms/src/worms_engine.cpp` — Transport-driven block processing, step boundary iteration
- `plugins/worms/src/worms_serialization.cpp/hpp` — Controls and pattern text serialization
- `plugins/worms/tests/worms_core_tests.cpp` — 18 deterministic tests, all passing
- `plugins/worms/CMakeLists.txt` — Core library + test target
- Root `CMakeLists.txt` — DOWNSPOUT_BUILD_WORMS option + subdirectory added

### Phase 2 — DPF Wrapper ✓ COMPLETE
- `plugins/worms/src/dpf/DistrhoPluginInfo.h` — Plugin metadata (ToneWorm, unique ID TnWm)
- `plugins/worms/src/dpf/worms_params.hpp` — kParamSpecs array (20 parameters)
- `plugins/worms/src/dpf/WormsPlugin.cpp` — DPF wrapper, Conductor CC extraction, state serialization

### Phase 3 — UI ✓ COMPLETE
- `plugins/worms/src/dpf/WormsUI.cpp` — GenerativePanelUI subclass with 6 rule choice grids

### Phase 4 — Build Integration ✓ COMPLETE
- install.sh — DOWNSPOUT_BUILD_WORMS=ON added
- scripts/package-release.sh — cmake flag + worms.vst3 in required_bundles
- .github/workflows/release.yml — worms.vst3 in release notes
- scripts/capture-plugin-screenshots.sh — worms:worms entry added
- docs/pages/_products/worms.md — created
- docs/summary.md, README.md, docs/install.md — ToneWorm added

### Phase 5 — Documentation & Screenshot ☐ PENDING
- Screenshot capture and visual review (requires build + xvfb-run)

---

## Key design decisions

- Tonnetz: (q,r) coords, pitch class = ((q*7+r*4)%12+12)%12
- Worm rule: turn[D] ∈ {0..4} = {L120, L60, Fwd, R60, R120} relative to incoming direction D
- Fixed pattern length (16/32/64/128 steps), transport-aligned loops
- Scale quantization uses same 23-scale table as BassGen/Melgen (append-only, docs/scales.md)
- Conductor CC: 21→density, 22→velocity, 23→vary, 24→mutate (same as BassGen)
- Melgen integration: route MIDI output → Melgen MIDI input (no code changes needed)
