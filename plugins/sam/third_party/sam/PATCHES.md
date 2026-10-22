# Local changes to the vendored SAM sources

SAM was written for a 6502 where every index is a byte and a wrap is free. Called from C it
indexes past arrays, divides by zero and, on one input, never returns. The plugin runs it on a
host's audio thread with text a user typed, so those were fixed here. All changes carry a
`Downspout:` comment. A 200,000-iteration fuzz run over both input modes and the full parameter
range (letters, vowels only, punctuation and digits, words up to 48 characters, with
AddressSanitizer and UndefinedBehaviorSanitizer) is clean after them; the plugin's own tests also
pass under both.

| File | Change | Why |
|---|---|---|
| all `.c` / `.h` | CRLF to LF; `sam.c` from ISO-8859-1 to UTF-8 | Reviewable diffs |
| `sam.c` `InsertBreath()` | With no pause to split at, reset the run length; a split point is used once | A long word (30+ letters of English, 48 of phonemes) wrote over the end marker at `phonemeindex[255]` and looped forever |
| `sam.c` `PrepareOutput()` | Render and restart before the 60-entry output tables fill | Unchecked `destpos++` overran them |
| `sam.c` `Parser1()` | Ignore a stress mark before any phoneme | Wrote `stress[-1]` |
| `sam.c`, `reciter.c` | `[pos-1]`, `[X-1]`, `[mem59-1]` wrap as a byte | Read before the array at position 0 (the 6502 wrapped to 255) |
| `sam.c`, `reciter.c` | `stress`, `phonemeLength`, `phonemeindex`, `inputtemp` padded to 264 | Rules look a few entries ahead |
| `SamTabs.h`, `RenderTabs.h` | Phoneme-indexed tables padded to 256 entries | A byte index (255 is the end marker) read past an 81-entry table; the padding reads as 0 |
| `render.c`, `processframes.c`, `createtransitions.c` | Frame arrays padded to 264 | Interpolation reads one past the end |
| `createtransitions.c` | `interpolate()` returns on zero width; pitch indices wrap as bytes | Divide by zero; read of `pitches[-1]` |
| `render.c` `Output()` | Bounds check against `SAM_BUFFER_BYTES` (`render.h`) | Wrote past the buffer on long phrases |
| `render.c` | `oldtimetableindex` moved out of `Output()` and reset by `ResetOutput()` | A render depended on the one before it |
| `sam.c` `Init()` | Static buffer filled with silence instead of `malloc` per render | The original leaked 220 kB each call, and byte 0 was never written |
| `sam.c`, `sam.h` | `SetSingmode(int)` | The original could switch singing on but not off |

`main.c` (the command-line program, WAV writer and SDL output) is not included. `debug.c` is, since
the debug branches still reference it; its `debug` switch is defined in `src/sam_speech.cpp`.
