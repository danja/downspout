# ToneWorm — Implementation Notes

## Architecture decisions

- Follows the BassGen pattern: portable core + thin DPF wrapper + NanoVG UI
- Uses `downspout::generative::ParamSpec` from generative-common (header-only)
- Does NOT inherit `TransportSnapshot` from BassGen; defines its own identical struct
- `Meter` is used only for `stepsPerBar` display/alignment, not for pattern structure
- Pattern length is a fixed parameter (16/32/64/128 steps), not auto-detected cycle

## Tonnetz coordinate system

- (q, r) where q=fifths axis, r=major-thirds axis
- Pitch class = ((q*7 + r*4) % 12 + 12) % 12
- Six directions: E(+1,0), NE(0,+1), NW(-1,+1), W(-1,0), SW(0,-1), SE(+1,-1)

## Worm rule encoding

- turn[D] ∈ {0..4}: L120, L60, Straight, R60, R120 relative to incoming direction D
- kTurnOffset = {2, 1, 0, 5, 4} — mod-6 offsets applied to incoming direction index
- No rule value can produce the reverse direction (D+3 mod 6)

## MIDI note mapping

- reg=2 targets octave 4 (base note = rootPc + 5*12)
- Offset within octave = (pitchClass - rootPc + 12) % 12

## Scale quantization

- After computing Tonnetz pitch class, find nearest interval in selected scale
- Scale table matches BassGen/Melgen (23 scales, camelCase enum, append-only)

## Conductor CC mapping (same as BassGen)

- CC 21 → density, CC 22 → velocity, CC 23 → vary, CC 24 (127) → mutate
