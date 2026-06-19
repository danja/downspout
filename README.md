# downspout

A bunch of mostly generative, algorithmic plugins based around DPF and VST3.

The repository is organized around portable C++ cores, deterministic tests, thin
DPF wrappers, and custom NanoVG UIs. VST3 metadata is normalized across the
plugins with creator `danja`, group `Downspout`, and plugin-specific categories.

Rough demos -

* [Generative jazz](https://www.youtube.com/shorts/6cv-nQ_2TNM) 
* [Another example](https://www.youtube.com/watch?v=DrAHdaJolyc) 

See also : [flues](https://github.com/danja/flues) LV2 plugins

## Install From Releases

Download `downspout-<version>-linux-x86_64-vst3.zip` from GitHub Releases,
unpack it, and copy the `.vst3` bundles into `~/.vst3`:

```bash
mkdir -p ~/.vst3
cp -r bassgen.vst3 p_mix.vst3 e_mix.vst3 m_mix.vst3 melgen.vst3 rift.vst3 orchid.vst3 ambo.vst3 drumgen.vst3 drumkit.vst3 cadence.vst3 counterpointer.vst3 sidecar.vst3 gremlin.vst3 gremlin_driver.vst3 ground.vst3 floozy.vst3 basilico.vst3 canticle.vst3 luma.vst3 paunchlad.vst3 lifeform.vst3 xoxolo.vst3 ~/.vst3/
```

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
| [melgen](plugins/melgen/README.md) | `melgen.vst3` | MIDI generator | Phrase-aware melody generator with contour, answer, structure, and follow controls. |
| [rift](plugins/rift/README.md) | `rift.vst3` | Audio effect | Transport-locked live/sample buffer disruptor with WAV loading, chop/stutter repeats, reverse, skip, smear, and pitch-slip actions. |
| [Orchid](plugins/orchid/README.md) | `orchid.vst3` | Audio effect | Transport-aware voiced freeze/hold effect with autocorrelation capture and grid-synced loop holds. |
| [Ambo](plugins/ambo/README.md) | `ambo.vst3` | Audio effect | Stereo ambient processor with rearrangeable time, spectral, tape, shimmer, delay, drive, and feedback modules. |
| [drumgen](plugins/drumgen/README.md) | `drumgen.vst3` | MIDI drum generator | Pattern generator with meter-aware styles, fills, and Breakbeat/Amen/Jungle/Hip Hop genres. |
| [drumkit](plugins/drumkit/README.md) | `drumkit.vst3` | Instrument | Port of the `flues` drum synth with stereo output, one MIDI input, and mixer-style UI. |
| [cadence](plugins/cadence/README.md) | `cadence.vst3` | MIDI effect | Transport-aware MIDI harmonizer and comping generator. |
| [counterpointer](plugins/counterpointer/README.md) | `counterpointer.vst3` | MIDI generator/effect | Learns incoming MIDI and emits a monophonic counter-melody. |
| [Sidecar](plugins/sidecar/README.md) | `sidecar.vst3` | MIDI generator | MIDI phrase player for generated solo material with local deterministic and localhost coordinator modes. |
| [gremlin](plugins/gremlin/README.md) | `gremlin.vst3` | Instrument | Chaotic glitch instrument with scenes, macros, actions, and performance controls. |
| [gremlin-driver](plugins/gremlin-driver/README.md) | `gremlin_driver.vst3` | MIDI effect | MIDI modulation and action sequencer intended to drive `gremlin`. |
| [ground](plugins/ground/README.md) | `ground.vst3` | MIDI generator | Long-form bass generator with Dub/Jazz styles, phrase planning, and guarded bass-register output. |
| [floozy](plugins/floozy/README.md) | `floozy.vst3` | Instrument | Corrected 8-voice hybrid physical/modulation synth derived from `floozy-poly`. |
| [basilico](plugins/basilico/README.md) | `basilico.vst3` | Instrument | Monophonic bass synth with Dub/Acid wobble, tempo sync, squelch, and upright/electric/industrial models. |
| [canticle](plugins/canticle/README.md) | `canticle.vst3` | Instrument | 12-voice keys, reed, pad, pluck, and glass synth for melody, counterpoint, and chords. |
| [luma](plugins/luma/README.md) | `luma.vst3` | MIDI generator | Launchpad-oriented performance generator where lit pads become bass, chord, melody, and drum agents. |
| [paunchlad](plugins/paunchlad/README.md) | `paunchlad.vst3` | Audio effect/instrument | Launchpad dub performance effect with echo throws, sirens, spring splashes, dropouts, and chops. |
| [lifeform](plugins/lifeform/README.md) | `lifeform.vst3` | MIDI generator | Conway Game of Life sequencer for Launchpad, evolving one generation per beat into melodic or drum MIDI. |
| [xoxolo](plugins/xoxolo/README.md) | `xoxolo.vst3` | MIDI generator | Simple x0x-style drum pattern editor with 11 drumkit lanes and a 32-step maximum. |

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

1. validate all twenty-three bundles in real hosts after clean installs and rescans;
2. keep release packaging aligned with local installs;
3. extend musical style vocabulary where it improves actual generator behavior;
4. publish and verify the first public tagged release artifact.
