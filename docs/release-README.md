# downspout

Mostly generative, algorithmic VST3 plugins built on DPF.

## Install All Plugins

This package contains one `.vst3` bundle per plugin. Copy **all** of them into
your VST3 folder, then restart your DAW or force a VST3 rescan.

Run the commands below from the unzipped folder.

**Linux** (`~/.vst3`):

```bash
mkdir -p ~/.vst3
cp -r *.vst3 ~/.vst3/
```

**macOS** (`~/Library/Audio/Plug-Ins/VST3`, or `/Library/Audio/Plug-Ins/VST3`
for all users):

```bash
mkdir -p ~/Library/Audio/Plug-Ins/VST3
cp -r *.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

**Windows** (`C:\Program Files\Common Files\VST3`, run PowerShell as
Administrator):

```powershell
Copy-Item -Recurse -Force *.vst3 "C:\Program Files\Common Files\VST3\"
```

## Notes

- The macOS and Windows builds are currently untested, and those packages omit
  `sidecar.vst3`.
- macOS may quarantine unsigned downloads. If a plugin is blocked, run:
  `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/*.vst3`
- Some hosts cache plugin names, makers, and categories independently of the
  installed bundle. If an old name or category persists, clear the host's
  plugin cache and rescan.

## More

- Plugin list, documentation, and source: https://github.com/danja/downspout
- Bug reports: https://github.com/danja/downspout/issues
- Licence: see `LICENSE` in this package.
