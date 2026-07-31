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
- `basilico.png`
- `canticle.png`
- `luma.png`
- `paunchlad.png`
- `lifeform.png`
- `tuney-vst.png`

The output path can be overridden:

```bash
DOWNSPOUT_SCREENSHOT_OUTPUT_DIR=docs/site/assets/plugins scripts/capture-plugin-screenshots.sh
```

## Pages site

The GitHub Pages source lives under `docs/pages/`. Plugin pages are Markdown
files in `docs/pages/_products/`, and each page references one of the generated
screenshots in `docs/pages/assets/plugins/`.
