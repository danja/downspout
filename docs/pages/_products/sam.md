---
title: Sam
order: 217
bundle: sam.vst3
kind: Instrument
category: instrument
role: Speech synthesiser
screenshot: /assets/plugins/sam.png
capabilities: [audio output, MIDI note input, host transport]
summary: The 1982 Software Automatic Mouth speech synthesiser. Type text, then play it from MIDI, one word per note sung at the key's pitch, or let the host transport read it aloud, one word per step.
---

## Opinion

Not yet auditioned in a host; the speech itself has been checked only through the tests and by
measurement. SAM is the voice of an 8-bit home computer: flat, buzzy and unmistakable, and that is
the point. Expect it to be best as a robot lead or a rhythmic spoken part, with **Tone** pulled down
a little to take the edge off its 22 kHz aliasing.

It is a speech synthesiser first: the words are always SAM's own. The musical part is how they are
timed and pitched, and in **Keys** mode a melody sung on a vowel-rich text sounds more like a vocoder
than a singer.

SAM's source has no licence (it is reverse-engineered abandonware), so this plugin is built from a
vendored, patched copy whose redistribution terms are unresolved.

## Functionality

Type the text into the box. Words are separated by spaces; a lone `_` is a rest. **Read it as**
chooses **English**, which SAM spells out by its own rules (`.` falls and `?` rises in pitch), or
**Phonemes**, where you write SAM's codes directly, such as `/HEH3LOW2 WER4LD`.

**Keys** mode is a MIDI instrument. Each note speaks one word at the pitch of the key, tuned to
within about 12 cents. **Next word** takes the words in turn and starts again when the host starts
playing; **Key = word** speaks the word a key selects, with C3 the first. Velocity sets level, pitch
bend moves the voice two semitones, **Release** sets the fade when a key comes up, and eight notes
can sound at once.

**Transport** mode is text-to-speech locked to the song. Word *n* starts on step *n* counted from
**Start at** (a bar), with steps from a sixteenth note to four bars, following tempo and time
signature. **Loop** repeats the text. Jump the playhead, or let the host loop, and the word under it
is spoken from the matching point. It is silent while the host is stopped: **Speak** plays one pass
without the transport, to hear what you have typed.

Both modes use SAM's four voice controls, **Speed**, **Pitch**, **Mouth** and **Throat**, and the six
voices from its manual (SAM, Elf, Robot, Stuffy, Old Lady, Alien) are one-click presets of them.
**Monotone** sings each word at one pitch instead of letting SAM shape it from stress and punctuation.
The current word is highlighted in the box while it plays.

The text is saved with the project. A word is rendered on the audio thread when it starts (a short one
takes about 40 microseconds), so nothing is allocated and the timing is sample-accurate.
