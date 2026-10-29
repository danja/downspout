# Clatter design notes

Portable core (`clatter_engine`, `clatter_serialization`, `clatter_patches`) behind a thin DPF wrapper and a
Magneto-look panel.

## Structure
- `clatter_patches.hpp` holds the table: one `Patch` per GM note (frequency, decay, pan, choke group, three
  family-specific values, gain). Table order is the Sound parameter order: families in turn, then note.
- Five families share a voice struct; the engine renders one voice at a time. Envelopes are multiplied down
  once per sample, so there is no `exp()` in the hot loop (the guiro tooth shape is the one exception).
- Noise comes from a per-plugin xorshift32 (`clatter_rng.hpp`): three shifts per sample, deterministic.
- The shaker and clap bands use the shared trapezoidal SVF (`downspout/dsp/svf.hpp`, read-only).

## Families
- **Membrane**: sine with a pitch drop (time constant 45 ms), three circular-membrane overtones (x1.59, x2.14,
  x3.41) that decay faster, and a short low-passed noise click. Timbales add a metallic ring. Mute and open
  variants are table entries (damping), not separate models.
- **Metal**: up to six inharmonic partials from one of four sets (agogo, triangle, jingle bell, bell tree), each
  decaying faster than the one below. Restrikes re-excite the partials; the bell tree steps up 12 % per strike.
  Partials above about 0.45 x the sample rate are left out rather than aliased.
- **Wood**: a fundamental, an overtone at x2.67 and a click. Castanets restrike twice, 13 ms apart.
- **Shaker**: noise through a band-pass with a ramped attack. Vibraslap adds a 52 Hz amplitude modulation (rattle).
- **Clap + guiro**: the clap is four noise bursts 9 ms apart, then a tail. The guiros drag a tooth pattern
  (58 / 46 Hz) across the band over the sound's length.

## Controls
- **Tune** scales frequency (and the band centre for shakers, clap and guiros).
- **Decay** scales the 60 dB time (a guiro's length).
- **Tone** is `2 x tone` as a brightness gain: membrane overtone and click level, metal upper-partial weights
  (`gain^(0.35 i)`), wood overtone and click; shakers, clap and guiros move the band by up to an octave either way.
- Trims are read when a hit starts, so changing one never alters a note already sounding.
- **Width** scales each sound's fixed pan (`pan x width / 0.6`, clamped), equal-power. 0 is mono.
- **Pan** (one per family, -1..1, default 0) is added to the sound's pan after Width and the sum is clamped, so it moves a
  whole family left or right of its designed placement; with Width at 0 it is the absolute position. Pans are the last
  five parameters (after Width), so projects saved before they existed keep their parameter IDs.

## Levels
Each patch has a gain that makes its peak about 0.5 at the default level (Level 0.8). The test holds every sound
between 0.2 and 0.75 and checks that each ends rather than droning.

## Assumptions
- Notes outside the table are ignored in General MIDI mode; there are no whistles or cuicas, and no GM2
  effects (27-34).
- A new hit in a choke group fades the others over 3 ms instead of cutting them.
- Same-sound retriggers overlap (the tail rings on); only the 16-voice limit cuts them.
- No transport use, no plugin state.
