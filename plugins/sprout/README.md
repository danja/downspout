# Sprout

L-system MIDI generator. A grammar is rewritten for N generations, the string is read
as a turtle, and the result plays as a monophonic, scale-quantised line locked to the
host transport. The idea comes from [Subsequence](https://subsystem.co/subsequence)
(see `docs/subsequent.md`); the grammars, interpretation and code are original.

| Symbol | Meaning |
|---|---|
| `F`, `G` | note, then advance one step |
| `f` | rest, advance one step |
| `+`, `-` | pitch up or down by Step size scale degrees |
| `[`, `]` | start a branch / return to the pitch it left (softer, up to 32 deep) |
| other | variable: rewritten but silent |

Grammars: Plant, Koch, Dragon, Sierpinski, Cantor (rhythm only), Levy, Tree.
Up to 16 generations. Expansions longer than 131,072 symbols are not used; the deepest
generation that fits plays instead (Most generations each grammar reaches: Plant 7, Koch 6, Dragon 15, Sierpinski 9, Cantor 10, Levy 14, Tree 6.) and the slider says so. **Grow every** N bars starts at generation 1 and adds one per N bars,
restarting the pattern at each change. Scales follow `docs/scales.md` (the same 24 as
plank). The plugin is stateless against the transport, so it survives loops and jumps.

Not supported: a rule editor, polyphony, per-grammar pitch mapping options.

## Build

Part of the root build (`DOWNSPOUT_BUILD_SPROUT`, default ON):

```
cmake --build build --target sprout-vst3
ctest --test-dir build -R sprout
```

Run `env -u DISPLAY scripts/capture-plugin-screenshots.sh sprout` for the catalogue image.
