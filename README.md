# downspout

A bunch of mostly generative, algorithmic plugins based around DPF and VST3.

The repository is organized around portable C++ cores, deterministic tests, thin
DPF wrappers, and custom NanoVG UIs. VST3 metadata is normalized across the
plugins with creator `danja`, group `Downspout`, and plugin-specific categories.

Demo -

* [Jack's Dream](https://youtu.be/Rd-ACU0JUdo) 
* [Another example](https://www.youtube.com/watch?v=DrAHdaJolyc) 

See also : [flues](https://github.com/danja/flues) LV2 plugins

## Install From Releases

Download the zip for your platform from GitHub Releases. The macOS and
Windows builds are currently untested, and those packages omit `sidecar.vst3`:

- `downspout-<version>-linux-x86_64-vst3.zip`
- `downspout-<version>-macos-arm64-vst3.zip`
- `downspout-<version>-macos-x86_64-vst3.zip`
- `downspout-<version>-windows-x86_64-vst3.zip`

Unpack it, then copy the `.vst3` bundles to your system VST3 folder. On Linux:

```bash
mkdir -p ~/.vst3
cp -r *.vst3 ~/.vst3/
```

Typical VST3 install locations are `~/.vst3` on Linux,
`~/Library/Audio/Plug-Ins/VST3` on macOS, and
`C:\Program Files\Common Files\VST3` on Windows.

See [docs/install.md](docs/install.md) and [docs/release.md](docs/release.md)
for the local install and release packaging details.

## Build and Install

For local development installs:

```bash
./install.sh
```

This configures `./build`, builds the enabled plugins, runs `ctest`, and
installs VST3 bundles into `~/.vst3` by default.

Useful overrides:

```bash
DOWNSPOUT_VST3_DIR=/some/other/path ./install.sh
DOWNSPOUT_RUN_TESTS=0 ./install.sh
```

After reinstalling, restart your DAW or force a VST3 rescan. Some hosts cache
plugin names, makers, categories, and homepage metadata independently of the
installed bundle.

## Plugins

| Plugin | Bundle | Type | Notes |
| --- | --- | --- | --- |
| [bassgen](plugins/bassgen/README.md) | `bassgen.vst3` | MIDI generator | Transport-aware bass generator with persistent pattern state, style modes, and MIDI follow/dodge controls. |
| [p-mix](plugins/p-mix/README.md) | `p_mix.vst3` | Audio effect | Transport-aware probabilistic stereo gate/mix effect. |
| [e-mix](plugins/e-mix/README.md) | `e_mix.vst3` | Audio effect | Transport-aware Euclidean stereo gate with redesigned UI. |
| [m-mix](plugins/m-mix/README.md) | `m_mix.vst3` | MIDI effect | Transport-aware MIDI gate combining `p-mix` transitions with `e-mix` Euclidean blocks. |
| [t-mix](plugins/t-mix/README.md) | `t_mix.vst3` | Audio mixer | Eight mono input strips with level, pan, mute, solo, metering, and stereo master output. |
| [Mixgen](plugins/mixgen/README.md) | `mixgen.vst3` | MIDI control generator | Random, quasi-random, and Euclidean producer lanes for T-Mix channel gains. |
| [Loopdelay](plugins/loopdelay/README.md) | `loopdelay.vst3` | Audio effect | Stereo delay/capture looper with free or BBT-locked time and MIDI-controlled time/feedback. |
| [melgen](plugins/melgen/README.md) | `melgen.vst3` | MIDI generator | Phrase-aware melody generator with contour, answer, structure, and follow controls. |
| [rift](plugins/rift/README.md) | `rift.vst3` | Audio effect | Transport-locked live/sample buffer disruptor with WAV loading, chop/stutter repeats, reverse, skip, smear, and pitch-slip actions. |
| [Orchid](plugins/orchid/README.md) | `orchid.vst3` | Audio effect | Transport-aware voiced freeze/hold effect with autocorrelation capture and grid-synced loop holds. |
| [Ambo](plugins/ambo/README.md) | `ambo.vst3` | Audio effect | Stereo ambient processor with rearrangeable time, spectral, tape, shimmer, delay, drive, and feedback modules. |
| [drumgen](plugins/drumgen/README.md) | `drumgen.vst3` | MIDI drum generator | Pattern generator with meter-aware styles, fills, and Breakbeat/Amen/Jungle/Hip Hop genres. |
| [drumkit](plugins/drumkit/README.md) | `drumkit.vst3` | Instrument | Port of the `flues` drum synth with stereo output, one MIDI input, and mixer-style UI. |
| [cadence](plugins/cadence/README.md) | `cadence.vst3` | MIDI effect | Transport-aware MIDI harmonizer and comping generator. |
| [arpgen](plugins/arpgen/README.md) | `arpgen.vst3` | MIDI effect | Transport-synced chord-capture and scale-derived arpeggiator. |
| [counterpointer](plugins/counterpointer/README.md) | `counterpointer.vst3` | MIDI generator/effect | Learns incoming MIDI and emits a monophonic counter-melody. |
| [Sidecar](plugins/sidecar/README.md) | `sidecar.vst3` | MIDI generator | MIDI phrase player for generated solo material with local deterministic and localhost coordinator modes. |
| [gremlin](plugins/gremlin/README.md) | `gremlin.vst3` | Instrument | Chaotic glitch instrument with scenes, macros, actions, and performance controls. |
| [gremlin-driver](plugins/gremlin-driver/README.md) | `gremlin_driver.vst3` | MIDI effect | MIDI modulation and action sequencer intended to drive `gremlin`. |
| [ground](plugins/ground/README.md) | `ground.vst3` | MIDI generator | Long-form bass generator with Dub/Jazz styles, phrase planning, and guarded bass-register output. |
| [floozy](plugins/floozy/README.md) | `floozy.vst3` | Instrument | Corrected 8-voice hybrid physical/modulation synth derived from `floozy-poly`. |
| [basilico](plugins/basilico/README.md) | `basilico.vst3` | Instrument | Monophonic bass synth with Dub/Acid wobble, tempo sync, squelch, and upright/electric/industrial models. |
| [canticle](plugins/canticle/README.md) | `canticle.vst3` | Instrument | 12-voice keys, reed, pad, pluck, and glass synth for melody, counterpoint, and chords. |
| [tuney-vst](plugins/tuney-vst/README.md) | `tuney_vst.vst3` | Instrument/MIDI generator | Turns focused typing or stored text into microtonal synthesized audio and ordinary MIDI notes. |
| [luma](plugins/luma/README.md) | `luma.vst3` | MIDI generator | Launchpad-oriented performance generator where lit pads become bass, chord, melody, and drum agents. |
| [paunchlad](plugins/paunchlad/README.md) | `paunchlad.vst3` | Audio effect/instrument | Launchpad dub performance effect with echo throws, sirens, spring splashes, dropouts, and chops. |
| [lifeform](plugins/lifeform/README.md) | `lifeform.vst3` | MIDI generator | Conway Game of Life sequencer for Launchpad, evolving one generation per beat into melodic or drum MIDI. |
| [xoxolo](plugins/xoxolo/README.md) | `xoxolo.vst3` | MIDI generator | Simple x0x-style drum pattern editor with 11 drumkit lanes and a 32-step maximum. |
| [Harmonic Atlas](plugins/harmonic-atlas/README.md) | `harmonic_atlas.vst3` | MIDI generator | Autonomous tonal, modal, chromatic-mediant, and neo-Riemannian-inspired harmony. |
| [Conductor](plugins/conductor/README.md) | `conductor.vst3` | MIDI generator | Long-form section and scene-command generator. |
| [Drift](plugins/drift/README.md) | `drift.vst3` | MIDI modulator | Four seeded CC lanes with LFO, sample-and-hold, walk, chaos, and follower modes. |
| [Mnemosyne](plugins/mnemosyne/README.md) | `mnemosyne.vst3` | MIDI effect | Fixed-capacity motif capture, memory, transformation, and recombination. |
| [Polymeter](plugins/polymeter/README.md) | `polymeter.vst3` | MIDI generator | Four Euclidean lanes with coprime lengths, ratchets, probability, and drift. |
| [Oracle](plugins/oracle/README.md) | `oracle.vst3` | Audio/MIDI effect | Bounded audio/MIDI analysis with CC and guarded note responses. |
| [Mosaic](plugins/mosaic/README.md) | `mosaic.vst3` | Sampler instrument | Four-slot WAV sampler with deterministic slicing and autonomous triggering. |
| [Resonance Garden](plugins/resonance-garden/README.md) | `resonance_garden.vst3` | Audio effect | MIDI-tuned damped resonator bank with internal-scale fallback. |
| [Orbit](plugins/orbit/README.md) | `orbit.vst3` | Audio effect | Seeded transport-aware stereo trajectories, distance filtering, and conservative Doppler. |
| [Guardian](plugins/guardian/README.md) | `guardian.vst3` | Safety effect | DC removal, look-ahead limiting, true-peak protection, and latched diagnostics. |

## Architecture

The repeated implementation pattern is:

1. keep DSP, MIDI, transport, and state behavior in a host-agnostic C++ core;
2. cover deterministic core behavior with tests;
3. translate host timing, parameters, MIDI, and state in a thin DPF wrapper;
4. keep UI code plugin-specific and driven by wrapper parameters/status.

The shared code lives under [include/downspout](include/downspout/) and
[src/common](src/common/). Plugin-specific implementations live under
[plugins](plugins/).

The repository now has a shared meter model so transport-aware generators can
distinguish simple, triple, compound, and odd groupings. Musical style behavior
is still uneven by design: `bassgen` and `drumgen` have explicit style modes,
while deeper pickup, phrase-ending, and folk-idiom behavior remains future work.

## Reference Docs

- [docs/process.md](docs/process.md): AI-assisted development workflow,
  instructions and skills, testing, builds, publishing, and definition of done.
- [docs/architecture.md](docs/architecture.md): build graph, layering, state,
  transport, UI, install, and release conventions.
- [docs/install.md](docs/install.md): local build and VST3 install behavior.
- [docs/release.md](docs/release.md): release artifact shape and GitHub Actions
  workflow.
- [docs/screenshots.md](docs/screenshots.md): automated UI screenshot capture
  for GitHub Pages assets.
- [docs/meter.md](docs/meter.md): shared meter model and current musical
  limitations.
- [docs/requirements.md](docs/requirements.md): build and dependency
  expectations.
- [docs/plan.md](docs/plan.md): project direction and remaining work.

## Current Work

All current wrapper targets build as VST3 bundles. The main remaining work is
host validation, release validation, and incremental DAW-facing fixes rather
than large architecture changes.

Near-term priorities:

1. validate all twenty-five bundles in real hosts after clean installs and rescans;
2. keep release packaging aligned with local installs;
3. extend musical style vocabulary where it improves actual generator behavior;
4. publish and verify the first public tagged release artifact.

## Documentation

There's a mess of rough drafts in the /docs dir plus :

**GitHub Pages** is built from docs/pages, not from the root docs.

  The editable plugin screenshot text is here:

  - Card text and plugin-page summary: summary: in each file under docs/pages/_products
  - Longer plugin-page notes: Markdown body below the front matter in the same file
  - Screenshot path: screenshot: in the same front matter, pointing at docs/pages/assets/plugins

  Example: docs/pages/_products/bassgen.md:1

  summary: Transport-synced bassline generator...
  ---

  ## Functionality

  BassGen creates...

  How it renders:

  - Index cards use plugin.summary in docs/pages/index.md:17
  - Individual plugin pages use page.summary, page.screenshot, then the Markdown body in docs/pages/_layouts/plugin.html:10
  - Jekyll collection config is in docs/pages/_config.yml:6

  Build/deploy path:

  - GitHub Actions workflow: .github/workflows/pages.yml:1
  - It runs actions/jekyll-build-pages with source: docs/pages
  - It deploys the generated _site artifact to GitHub Pages

  For local preview from repo root:

  jekyll serve --source docs/pages --destination /tmp/downspout-pages-site

  Screenshot generation is separate. The PNGs are regenerated by scripts/capture-plugin-screenshots.sh, documented in docs/screenshots.md:1, but the descriptive text is in _products/*.md.
