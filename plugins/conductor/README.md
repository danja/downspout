# Conductor

Conductor emits bar-aligned block notes and scene, density, energy, mutation,
and reset CC values. Edit up to eight blocks from Null, Intro, Theme, Develop,
Break, Reprise and Coda; choose a fixed repeating form or seeded weighted
selection from the active block types. Durations are bounded and the form is
rebuilt deterministically after a seek. It never changes the host graph.

The block strip shows the current and queued blocks. Click a block to edit its
type, then use **Jump here** to queue it for the next bar. The panel's
**Reroll** button beside **Form seed** commits a fresh seed in 1–65535.

Scene values and the control table are shared with receivers through
`include/downspout/conductor_protocol.hpp`; the defaults preserve the original
Intro/Develop/Break/Reprise/Coda values.
