# bassgen port

This directory holds the `downspout` port of `~/github/flues/lv2/bassgen`.

`bassgen` is a transport-aware MIDI bass generator with persistent pattern
state, loop-boundary variation, and a custom DPF UI. In `downspout`, the core
now stores normalized meter data and shapes rhythm around pulse starts,
pickups, and longer pulse-spanning notes rather than assuming every bar is
effectively `4/4`. It also now exposes explicit style modes:
`Auto`, `Straight`, `Reel`, `Waltz`, `Jig`, and `Slip Jig`.

`Offset` (0–1 beat, default 0) delays the whole pattern against the transport,
so notes land after the beat; 0.5 puts a downbeat note on the eighth-note
offbeat. It does not regenerate the pattern.

The DPF wrapper accepts incoming MIDI as context. The `Follow/Dodge` control
can make bass notes more likely on incoming drum/bass hits or suppress bass
notes on those beats. `Input Match` chooses whether input must match one exact
note, any note on the listen channel, or any note at all. `Input Sensitivity`
scales how strongly that input affects follow/dodge probability and, in channel
or any-note matching, how far injected follow notes are pulled toward a
companion interval above the incoming guide pitch.
Channel 10 keeps the exact listen-note trigger behavior used for drum-style
hits. Channel 1 note-ons are also accepted as musical guides: when follow
injection happens, their pitch steers the injected note toward a related
bass-register pitch instead of acting only as a trigger.
The `Color` control adjusts harmonic tension: for Jazz it moves dominant bars
from inside/bebop material toward altered, diminished, and whole-tone colors,
for Fugue it adds leading-tone pickup behavior, while other genres use it for
more blues, tritone, modal, or passing-note movement without changing the
selected scale.

Implementation status:

- portable core library exists and builds;
- deterministic tests exist and pass;
- host-neutral block engine exists for MIDI scheduling;
- text state serialization exists for controls, pattern, and variation;
- the DPF wrapper and custom UI build as `bassgen.vst3`;
- the core now handles shared-meter transport input and regenerates on meter
  changes;
- the core now has explicit style controls for simple, reel, waltz, jig, and
  slip-jig phrasing on top of the shared meter model;
- scale choices now include Lydian, Melodic Minor, Whole Tone, Altered,
  Half-Whole Diminished, Whole-Half Diminished, Bebop Dominant, Bebop Major,
  and Bebop Minor in addition to the original minor/major/modal/pentatonic/
  blues options. These were appended to the scale enum to preserve existing
  saved-state values;
- the appended Jazz genre biases rhythm toward walking beat anchors and maps
  four-bar phrases across ii-V-I-turnaround roots, with role-specific Dorian,
  Mixolydian/altered, and Major/Lydian color for ordinary scales. Dominant
  bars choose from Mixolydian, altered, half-whole diminished, whole-tone, and
  bebop dominant material, and beat-start notes target roots, thirds, fifths,
  and sevenths. One- and two-subdivision pickups add chromatic approaches and
  simple enclosures around the next chord-tone target while explicitly selected
  Jazz color scales remain constrained to their chosen vocabulary;
- the appended Fugue genre biases rhythm toward flowing subject steps and maps
  four-bar phrases across tonic subject, dominant answer, short episode, and
  tonic pedal/cadence behavior;
- the appended Rock genre preserves existing genre IDs while adding hard
  beat-start anchors, root/fifth/octave-biased note choice, longer holds, and
  stronger velocities for riff-like bass lines;
- the appended Moroder genre preserves existing genre IDs while adding a
  happy, repetitive subdivision-pulse bass line with tonic/fifth/octave focus
  and slider-driven color turns, density holes, short holds, and accents;
- the appended Descend/bar genre (ID 13) is the same line restarted in every bar: each bar steps down
  from the top of its own line and ends on the root, so the pattern repeats a falling figure rather
  than one long fall;
- the appended Descending genre (ID 12) plays one line down the scale across the
  whole pattern: the first event is the top of the line (up to about an octave and
  a half above the root) and the last always lands on the root, so a pattern with
  more notes simply steps down more slowly. `Color` is how far it wanders from
  that staircase: 0 follows it exactly and never rises; higher values let an
  event step back up, pause or dip ahead of the line, never leaping more than two
  scale degrees up. Most beat starts are pinned so the line keeps a pulse. The
  `Rhythm` action refits the same line to the new rhythm instead of copying notes
  by index;
- the general `Color` control is serialized and exposed in the UI, and affects
  Jazz dominant color, Fugue leading tones, and genre-specific tension choices
  for Funk, Acid, Dub, Ambient, Sabbath, Rock, Moroder, Descending (wander), and the electronic
  styles;
- incoming MIDI follow/dodge controls are wired through the wrapper, core, and
  UI, including channel/any-note input matching and input sensitivity for
  Ground-guided companion lines.

Reference docs:

- `docs/audit.md`
- `docs/extraction-plan.md`
- `docs/porting.md`
