# Ground

Ground is a transport-synced MIDI bass generator. It does not process audio.
Put it on a MIDI/instrument track, route its MIDI output to a synth or sampler,
press play in the host, and Ground emits a monophonic bass line that follows
the host tempo, bar position, meter, and loop/rewind position.

Ground is built for longer musical movement rather than one short riff. It
plans a complete section, assigns a musical role to each part of that section,
then generates notes for those roles. The top lane in the UI shows that plan
and lets you override each role directly.

## How to Use It

1. Choose `Root`, `Scale`, and `Register` in the Frame panel.
2. Choose a `Form Shape`. Use `Free` for the generic planner, or a named shape
   for a fixed structure such as blues, fugue, AABA, techno, dub, ambient, or
   rondo.
3. Set `Form` and `Phrase` if the shape is `Free`. Named shapes choose these
   values for you.
4. Adjust the Motion sliders for density, movement, note length, and range.
5. Press `New Form` when you want a fresh whole-section plan.
6. Press `New Phrase` to rewrite the currently playing phrase.
7. Press `Mutate Cell` to keep the current role but rewrite its local note
   pattern.
8. Click a role cell in the top lane to force a phrase to `Statement`,
   `Answer`, `Climb`, `Pedal`, `Breakdown`, `Cadence`, or `Release`. Choose
   `Auto` to return that phrase to the generated role.

## Terminology

`Form` means the full planned section length in bars. If Form is 16, Ground is
planning a 16-bar section before it loops back to the start.

`Phrase` means one musical chunk inside the form. If Form is 16 and Phrase is
4, the form has four phrases. The top preview lane shows one cell per phrase.

`Role` means what a phrase is trying to do musically:

- `Statement`: introduces or restates the main bass idea.
- `Answer`: responds to the previous phrase, often reusing related material.
- `Climb`: pushes upward or increases energy.
- `Pedal`: holds around a stable low pitch.
- `Breakdown`: thins the line out.
- `Cadence`: aims at a stronger ending.
- `Release`: resolves or relaxes instead of making a hard cadence.

`Cell` means the small local note pattern inside the current phrase. `Mutate
Cell` changes that local pattern without changing the phrase role.

## What Happens Under the Hood

Ground first builds a phrase plan: length, role, root movement, register lift,
intensity, and motion bias for each phrase. Named `Form Shape` values use
explicit templates. `Free` uses the current controls to choose roles and arcs.

After the phrase plan is built, Ground generates note events. `Density` decides
how many onsets appear, `Motion` and `Tension` influence degree movement, and
`Style` changes the rhythmic feel. `Dub` stays sparse and heavy, while `Jazz`
leans toward walking-bass motion and approach tones.

`Sequence` lets answer/release phrases derive material from the previous
phrase. With high `Sequence` and high `Cadence`, the planner enters a
fugue-friendly area with subject, answer, pedal, and cadence behavior while
keeping the same visible controls.

`Color` adds harmonic and melodic tension. At higher values, Ground may choose
more active phrase behavior, use more motion on compatible scales, and add
occasional chromatic pickups.

Generated notes are folded into a bass-friendly lane after form generation,
phrase refresh, cell mutation, and loop variation. `Register` picks the base
octave lane, `Register Arc` allows the line to rise across the form, and
`Clamp` folds notes by octaves into a fixed semitone range above the selected
Root in the selected Register. With the default Clamp of 12, notes stay within
one octave.

Ground stores both the generated form and the controls as text state. Given the
same state and seed, generation is deterministic.

---

## Control Cheatsheet

### Frame

`Root`: MIDI root note and octave for the generated line.

`Scale`: pitch collection used when choosing notes.

`Style`: rhythmic feel. Grounded, Ostinato, March, Pulse, Drone, Climb, Dub,
and Jazz bias the local note pattern differently.

`Shape`: section template. `Free` follows Form/Phrase; named shapes impose a
known structure.

`Form`: total section length in bars when Shape is `Free`.

`Phrase`: phrase length in bars when Shape is `Free`.

`Register`: base octave lane for the bass line.

`Channel`: MIDI output channel.

### Motion

`Density`: more or fewer note starts.

`Motion`: how much the line moves between scale degrees.

`Tension`: where and how strongly the form builds toward a peak.

`Color`: extra harmonic/melodic tension and occasional chromatic pickup notes.

`Cadence`: stronger final-phrase ending versus softer release.

`Register Arc`: how much the register rises across the form.

`Clamp`: semitone range above Root, folded by octaves. Default is 12.

`Sequence`: how much answer/release phrases borrow from the previous phrase.

`Note Length`: maximum hold length before the next onset.

`Note Length Variation`: variation around the generated hold length.

`Vary`: amount of automatic change after completed form loops.

`Seed`: deterministic random seed.

### Actions

`New Form`: regenerate the whole section.

`New Phrase`: regenerate the currently playing phrase.

`Mutate Cell`: rewrite the local pattern inside the current phrase.

### Top Lane

Each cell is one phrase. The label shows the phrase role. Click a cell to force
a role, or choose `Auto` to let Ground plan that phrase.
