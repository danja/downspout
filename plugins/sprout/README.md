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

Grammars: Plant, Koch, Dragon, Sierpinski, Cantor (rhythm only), Levy, Tree, Custom.
Up to 16 generations. Expansions longer than 131,072 symbols are not used; the deepest
generation that fits plays instead (Most generations each grammar reaches: Plant 7, Koch 6, Dragon 15, Sierpinski 9, Cantor 10, Levy 14, Tree 6.) and the slider says so. **Grow every** N bars starts at generation 1 and adds one per N bars,
restarting the pattern at each change. Scales follow `docs/scales.md` (the same 24 as
plank). The plugin is stateless against the transport, so it survives loops and jumps.

**Custom** plays a grammar you type into the Rules field. Click the field (a click on a
built-in's rules copies them in as a starting point), then write the starting string and
the rules separated by `;` or a new line, e.g. `F; F -> F[+F]F` or `X; F -> FF; X -> F+[[X]-X]-F[-FX]+X`.
A rule's left side is one letter, its right side letters and `+ - [ ]` (empty deletes the letter);
`=` works as well as `->`. Up to 16 rules and 240 characters. It applies as you type, and text that
does not parse is flagged under the field while the last valid grammar keeps playing. Ctrl+V pastes
(new lines become `;`), Enter or a click elsewhere finishes. The text is saved with the project as the
`grammar` plugin state.

Not supported: polyphony, per-grammar pitch mapping options.

## Build

Part of the root build (`DOWNSPOUT_BUILD_SPROUT`, default ON):

```
cmake --build build --target sprout-vst3
ctest --test-dir build -R sprout
```

Run `env -u DISPLAY scripts/capture-plugin-screenshots.sh sprout` for the catalogue image.
