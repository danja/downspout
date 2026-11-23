# plugin screenshots

Plugin screenshots for GitHub Pages are generated from the real DPF UI code,
not from mockups.

## Script

Use:

```bash
scripts/capture-plugin-screenshots.sh
```

By default this:

- configures `build/screenshots` with `DOWNSPOUT_BUILD_SCREENSHOT_APPS=ON`;
- builds DPF standalone UI targets for all plugins;
- starts a temporary `jackd -d dummy` server when no JACK server is already
  running, so screenshots do not require an audio device;
- launches each UI under Xvfb when no `DISPLAY` is already available;
- captures each window with ImageMagick;
- writes PNGs to `docs/pages/assets/plugins/`.
- exports `DOWNSPOUT_SCREENSHOT_DEMO=1`. A standalone has no audio, so a panel that
  only means something with signal (currently `sinevox`, whose plot is the tracked
  formants) reads this and shows sample data. Hosts never set it.

To capture only selected plugins:

```bash
scripts/capture-plugin-screenshots.sh bassgen drumgen drumkit
```

To reuse an existing screenshot build:

```bash
scripts/capture-plugin-screenshots.sh --skip-build
```

The reused build must have been configured with
`DOWNSPOUT_BUILD_SCREENSHOT_APPS=ON`, because screenshot builds enable DPF's
standalone UI target.

## Linux dependencies

The script expects:

- CMake
- Xvfb when running headless
- JACK tools, usually `jackd2`
- either `xdotool` or the `xwininfo`/`xprop` tools from `x11-utils`
- ImageMagick `import`

On Debian/Ubuntu:

```bash
sudo apt install jackd2 xvfb xdotool x11-utils imagemagick
```

## Outputs

Current output filenames:

- `bassgen.png`
- `p-mix.png`
- `e-mix.png`
- `m-mix.png`
- `t-mix.png`
- `mixgen.png`
- `loopdelay.png`
- `lightverb.png`
- `melgen.png`
- `rift.png`
- `orchid.png`
- `drumgen.png`
- `drumkit.png`
- `xoxolo.png`
- `cadence.png`
- `arpgen.png`
- `counterpointer.png`
- `sidecar.png`
- `gremlin.png`
- `gremlin-driver.png`
- `ground.png`
- `ambo.png`
- `floozy.png`
- `fontana.png`
- `basilico.png`
- `canticle.png`
- `magneto.png`
- `beards.png`
- `trajectory.png`
- `bore.png`
- `box.png`
- `fathom.png`
- `qune.png`
- `polyscribe.png`
- `splay.png`
- `gate.png`
- `tilt.png`
- `prism.png`
- `mouth.png`
- `grist.png`
- `sam.png`
- `ladder.png`
- `glue.png`
- `octet.png`
- `trio.png`
- `infra.png`
- `spindown.png`
- `wheelhouse.png`
- `whirligig.png`
- `chorale.png`
- `silken.png`
- `fresco.png`
- `strata.png`
- `quartet.png`
- `monochord.png`
- `nam.png`
- `thumper.png`
- `snatch.png`
- `tops.png`
- `clatter.png`
- `spense.png`
- `heavyverb.png`
- `interleaver.png`
- `primefold.png`
- `quefrency.png`
- `keyframe.png`
- `moka.png`
- `luma.png`
- `paunchlad.png`
- `lifeform.png`
- `plank.png`
- `padseq.png`
- `pratt.png`
- `retune.png`
- `sprout.png`
- `markov.png`
- `melevolve.png`
- `syrinx.png`
- `tuney-vst.png`
- `voxmod.png`
- `sinevox.png`
- `sieve.png`
- `zmorph.png`

This list is out of date in places: it predates several plugins, and
`docs/pages/_products/*.md` is the authoritative record of which catalog
entries carry a screenshot. Add new capture targets to
`scripts/capture-plugin-screenshots.sh` and the product page's `screenshot:`
front matter together, not just here.

The output path can be overridden:

```bash
DOWNSPOUT_SCREENSHOT_OUTPUT_DIR=docs/site/assets/plugins scripts/capture-plugin-screenshots.sh
```

## Pages site

The GitHub Pages source lives under `docs/pages/`. Plugin pages are Markdown
files in `docs/pages/_products/`, and each page references one of the generated
screenshots in `docs/pages/assets/plugins/`.

## Visual acceptance

The refreshed `melevolve.png` and `conductor.png` captures were accepted and
copied into `docs/pages/assets/plugins/` at the dimensions declared in each
plugin's `DistrhoPluginInfo.h`:

- `conductor` — 960x820, 16-block editor in two rows of eight.
- `melevolve` — 920x700, two-tab Melogen-style panel with a Favourite row.
- `padseq` — 980x720, SysEx-only LED feedback.
- `fontana` — 1280x800, browser scroll gutter and lazy preview.
- `bassgen` and `drumgen` — accepted captures copied into the catalog.

The Spliff panel was then changed again to make the analyser visible and to
remove the overlapping parameter-box footer. A fresh 720x680 capture is in
`/tmp/opencode/spliff-shots/spliff.png`; its analyser bars and slider rows were
pixel-checked, but this agent cannot view PNG pixels, so catalog acceptance is
pending. The catalog `spliff.png` is still the previous accepted capture.
