# Sam

Software Automatic Mouth: the 1982 Commodore 64 formant speech synthesiser, as an instrument.
Type some text, then either **play it from MIDI** (each note speaks the next word, sung at the
pitch of the key) or **let the host transport read it aloud** (one word per step from a start bar,
locked to the bars and beats). See `docs/design.md`.

SAM's code is vendored in `third_party/sam` and **has no licence** (abandonware; see
`third_party/sam/SAM-LICENSE.md`). It carries local fixes, listed in `third_party/sam/PATCHES.md`.

No audio input. Stereo output (the voice is mono, sent to both channels). One MIDI input.

## Modes

| Mode | What plays | Pitch | Needs |
|---|---|---|---|
| Keys | A note speaks one word; Advance picks which: **Next word** cycles through the text (and starts over when the transport starts), **Key = word** speaks word `(note - 48)` modulo the word count | The key, tuned (see below) | MIDI |
| Transport | Word *n* starts at step *n* counted from **Start bar**; **Loop** repeats the text. A word entered mid-step (after a locate or a loop wrap) is spoken from the matching point | **Pitch**, or the fixed **Monotone** pitch | Host transport. Silent while stopped; **Speak** plays one pass without it |

Words are the whitespace-separated pieces of the text. A lone `_` is a rest and still takes its slot.
**Read it as** chooses English (SAM's own spelling rules; `.` and `?` shape the pitch) or
**Phonemes** (`/HEH3LOW2 WER4LD`: SAM phoneme codes, digits 1-8 for stress, `/H` a breath).

## Controls

| Control | Range | Default |
|---|---|---|
| Mode | Keys / Transport | Keys |
| Speed | 20-200 (lower is faster) | 72 |
| Pitch | 16-255 (SAM units; Transport mode) | 64 |
| Mouth, Throat | 0-255 | 128, 128 |
| Monotone | Off / On (fixed pitch, as SAM's `-sing`; Transport mode) | Off |
| Phonetic | Off / On | Off |
| MIDI In | Off, Ch 1-16, All | All |
| Advance | Next word / Key = word | Next word |
| Transpose | -24 to +24 st | 0 |
| Release | 5-1000 ms | 60 ms |
| Step | 1/16, 1/8, 1/4, 1/2, 1/1, 1 bar, 2 bars, 4 bars | 1/4 |
| Start Bar | 1-256 | 1 |
| Loop | Off / On | On |
| Speak | trigger | |
| Tone | 1-12 kHz low-pass, top is open | 8000 Hz |
| Level | 0-1 | 0.70 |
| Current Word | read-only status: slot being spoken, 0 when idle | |

The panel also has the six voices from the SAM manual (SAM, Elf, Robot, Stuffy, Old Lady, Alien) as
presets of Speed, Pitch, Throat and Mouth.

MIDI: note on/off (velocity scales level), pitch bend (+-2 semitones), CC 120 and 123 release
everything. Eight voices, the oldest is stolen.

The text is saved with the project as the `text` state (up to 2000 characters, 512 words); every other
setting is a parameter.

## Things to know

- **Tuning.** SAM sets pitch as a whole number of ticks of 3.24 samples at 22.05 kHz, so the nearest
  pitch can be well off a key: up to about 0.7 semitone at C5 and about 2 semitones near B6. Each
  note is rendered at the nearest pitch and resampled the rest of the way; measured over C2 to B6
  the result is within 12 cents of the key. The resampling moves the voice's formants by the same
  ratio, so the top octave takes on a slightly smaller, brighter voice.
- **Sound.** SAM is 8-bit at 22.05 kHz. **Tone** softens the aliasing; set it to open for the raw sound.
- **CPU.** A word is rendered by SAM on the audio thread when it starts: about 30-50 us for a short
  word, roughly 1 ms per second of speech. A very long word (tens of characters) takes proportionally
  more. Nothing is allocated.
- **Limits.** One word is at most 48 characters. A word that runs past its step is cut by the next
  one, with a few milliseconds of fade.

## Build

Part of the root build (`DOWNSPOUT_BUILD_SAM`, default ON):

```
cmake --build build --target sam-vst3
ctest --test-dir build -R sam
```

Run `env -u DISPLAY scripts/capture-plugin-screenshots.sh sam` for the catalogue image.
