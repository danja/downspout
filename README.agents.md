# Downspout — Agent Reference

Downspout is a suite of VST3 plugins for autonomous music generation and processing built with DPF. This file is the entry point for linked-data discovery. Each plugin has a machine-readable RDF profile expressed in Turtle using the `trn:` vocabulary.

## RDF resources

| Resource | Location | Purpose |
|----------|----------|---------|
| Vocabulary | `../transmission/vocabs/profile.ttl` | Defines `trn:PluginProfile`, role taxonomy, signal types, routing properties (`trn:feedsControls`, `trn:recommendedBefore`, …), and CC mapping terms (`trn:ccMapping`, `trn:ccNumber`, `trn:targetParameter`, …). Namespace: `http://purl.org/stuff/transmissions/` |
| Consolidated profile | `../transmission/profiles/downspout.ttl` | All plugins in one graph. Plugin IRIs: `http://purl.org/stuff/transmissions/plugins/downspout/<id>` |
| Per-plugin profiles | `plugins/<name>/profile.ttl` (each below) | Fuller detail: CC mapping blank nodes, `foaf:homepage`, cautions |

## Plugins

### Structure / control

| Plugin | Role | Profile |
|--------|------|---------|
| [Conductor](plugins/conductor/) | Long-form section generator; emits scene, density, energy, mutation, and reset CCs to reshape generators | [profile.ttl](plugins/conductor/profile.ttl) |
| [Mixgen](plugins/mixgen/) | Transport-synced producer for the control bus; drives T-Mix gain lanes and effect macros via CC | [profile.ttl](plugins/mixgen/profile.ttl) |
| [Drift](plugins/drift/) | Four-lane transport-synced MIDI CC modulator (LFO, S&H, random-walk, chaos, envelope follower) | [profile.ttl](plugins/drift/profile.ttl) |
| [Oracle](plugins/oracle/) | Audio/MIDI analyser that extracts musical features and emits mapped CC values | [profile.ttl](plugins/oracle/profile.ttl) |

### MIDI generators

| Plugin | Role | Profile |
|--------|------|---------|
| [BassGen](plugins/bassgen/) | Transport-synced bass generator; accepts Conductor CCs and Follow/Dodge Note On input | [profile.ttl](plugins/bassgen/profile.ttl) |
| [Ground](plugins/ground/) | Long-form bass generator with phrase roles and arc tension; full Conductor CC integration including scene→role mapping | [profile.ttl](plugins/ground/profile.ttl) |
| [DrumGen](plugins/drumgen/) | Transport-aware drum generator with fills, genre vocabulary, and Conductor CC input | [profile.ttl](plugins/drumgen/profile.ttl) |
| [MelGen](plugins/melgen/) | Phrase-aware melody generator with contour and question/answer structure | [profile.ttl](plugins/melgen/profile.ttl) |
| [Harmonic Atlas](plugins/harmonic-atlas/) | Autonomous voice-led harmony generator with neo-Riemannian-inspired movement | [profile.ttl](plugins/harmonic-atlas/profile.ttl) |
| [ArpGen](plugins/arpgen/) | Transport-synced chord-capture and scale-derived arpeggiator | [profile.ttl](plugins/arpgen/profile.ttl) |
| [Polymeter](plugins/polymeter/) | Four-lane Euclidean MIDI generator with coprime lengths and phase drift | [profile.ttl](plugins/polymeter/profile.ttl) |
| [Xoxolo](plugins/xoxolo/) | Explicit x0x-style drum sequencer with 11 lanes and patterns up to 32 steps | [profile.ttl](plugins/xoxolo/profile.ttl) |
| [Luma](plugins/luma/) | Launchpad-oriented performance generator with bass, chord, melody, and drum agents | [profile.ttl](plugins/luma/profile.ttl) |
| [Lifeform](plugins/lifeform/) | Conway Game of Life sequencer mapping cellular generations to MIDI | [profile.ttl](plugins/lifeform/profile.ttl) |
| [Sidecar](plugins/sidecar/) | MIDI phrase player with deterministic local generation and optional coordinator mode | [profile.ttl](plugins/sidecar/profile.ttl) |
| [Tuney VST](plugins/tuney-vst/) | Text-to-music instrument with configurable alphabets, scales, and microtonal tunings | [profile.ttl](plugins/tuney-vst/profile.ttl) |

### MIDI processors

| Plugin | Role | Profile |
|--------|------|---------|
| [Cadence](plugins/cadence/) | Transport-aware MIDI harmonizer and comping generator | [profile.ttl](plugins/cadence/profile.ttl) |
| [Counterpointer](plugins/counterpointer/) | Learns incoming MIDI and emits a related monophonic counter-melody | [profile.ttl](plugins/counterpointer/profile.ttl) |
| [Mnemosyne](plugins/mnemosyne/) | MIDI phrase memory with capture, recall, transform, and recombination modes | [profile.ttl](plugins/mnemosyne/profile.ttl) |
| [M-Mix](plugins/m-mix/) | Transport-aware MIDI gate combining probabilistic transitions with Euclidean blocks | [profile.ttl](plugins/m-mix/profile.ttl) |
| [Gremlin Driver](plugins/gremlin-driver/) | MIDI modulation and action sequencer designed to control Gremlin | [profile.ttl](plugins/gremlin-driver/profile.ttl) |

### Audio effects

| Plugin | Role | Profile |
|--------|------|---------|
| [P-Mix](plugins/p-mix/) | Transport-aware probabilistic stereo gate | [profile.ttl](plugins/p-mix/profile.ttl) |
| [E-Mix](plugins/e-mix/) | Transport-aware deterministic Euclidean stereo gate | [profile.ttl](plugins/e-mix/profile.ttl) |
| [T-Mix](plugins/t-mix/) | Eight-input mono-strip mixer with level, pan, mute, solo, and metering | [profile.ttl](plugins/t-mix/profile.ttl) |
| [Loopdelay](plugins/loopdelay/) | Stereo delay and capture looper; BBT-syncable; accepts CC 30/31 for time and feedback | [profile.ttl](plugins/loopdelay/profile.ttl) |
| [Lightverb](plugins/lightverb/) | Minimal four-line FDN reverb; accepts CC 32/33 for wet and space | [profile.ttl](plugins/lightverb/profile.ttl) |
| [Rift](plugins/rift/) | Transport-locked buffer disruptor for chop, stutter, reverse, skip, smear, and pitch-slip | [profile.ttl](plugins/rift/profile.ttl) |
| [Orchid](plugins/orchid/) | Transport-aware voiced freeze effect with grid-synchronised loop holds | [profile.ttl](plugins/orchid/profile.ttl) |
| [Ambo](plugins/ambo/) | Stereo ambient processor with time, spectral, tape, shimmer, delay, drive, and feedback modules | [profile.ttl](plugins/ambo/profile.ttl) |
| [Resonance Garden](plugins/resonance-garden/) | Eight-voice damped resonator bank that turns stereo input into pitched material | [profile.ttl](plugins/resonance-garden/profile.ttl) |
| [Orbit](plugins/orbit/) | Transport-aware stereo motion effect with orbit, pendulum, random-walk, and figure-eight modes | [profile.ttl](plugins/orbit/profile.ttl) |
| [Gater](plugins/gater/) | MIDI-controlled stereo switcher routing input to one of two outputs by note parity | [profile.ttl](plugins/gater/profile.ttl) |
| [PaunchLad](plugins/paunchlad/) | Launchpad dub performance effect with echo throws, splashes, chops, and freezes | [profile.ttl](plugins/paunchlad/profile.ttl) |
| [Guardian](plugins/guardian/) | Output-safety processor with DC removal, look-ahead limiting, and true-peak protection | [profile.ttl](plugins/guardian/profile.ttl) |

### Instruments

| Plugin | Role | Profile |
|--------|------|---------|
| [Basilico](plugins/basilico/) | Monophonic bass instrument with upright, electric, dub, acid, and industrial models | [profile.ttl](plugins/basilico/profile.ttl) |
| [DrumKit](plugins/drumkit/) | Stereo synthesised drum instrument and natural sound source | [profile.ttl](plugins/drumkit/profile.ttl) |
| [Canticle](plugins/canticle/) | Twelve-voice keys, reed, pad, pluck, and glass instrument | [profile.ttl](plugins/canticle/profile.ttl) |
| [Floozy](plugins/floozy/) | Eight-voice hybrid physical/modulation synthesizer | [profile.ttl](plugins/floozy/profile.ttl) |
| [Gremlin](plugins/gremlin/) | Chaotic glitch instrument with scenes, macros, actions, and randomisation | [profile.ttl](plugins/gremlin/profile.ttl) |
| [Mosaic](plugins/mosaic/) | Four-slot WAV sampler with deterministic slicing and autonomous triggering | [profile.ttl](plugins/mosaic/profile.ttl) |
| [Syrinx](plugins/syrinx/) | Polyphonic avian vocal synthesizer using Mindlin-Laje ODE models | [profile.ttl](plugins/syrinx/profile.ttl) |

## Routing conventions

Key `trn:` properties that describe inter-plugin data flow:

- `trn:feedsControls` — this plugin sends CC or control MIDI that reshapes live parameters on the target (e.g. Conductor→BassGen, Mixgen→T-Mix)
- `trn:conductorChannelParam` — name of the receiver parameter that selects the Conductor CC listen channel (`conductor_ch`, 0 = off)
- `trn:recommendedBefore` / `trn:recommendedAfter` — suggested signal-chain ordering
- `trn:companion` — tightly coupled pair (e.g. GremlinDriver↔Gremlin)

Signal type individuals used in `trn:accepts` / `trn:produces`: `trn:Audio`, `trn:Midi`, `trn:ControlMidi`, `trn:BassMidi`, `trn:DrumMidi`, `trn:MelodyMidi`, `trn:HarmonyMidi`, `trn:MultiPartMidi`.
