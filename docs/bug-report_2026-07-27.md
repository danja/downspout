Nearly half of them crash Ableton Live on windows 11. Ill come back with the precise list of what works and what didnt, and spend some time testing the ones that do work.

edit: Ok here are the ones that crashed Ableton Live:

1. BassGen
2. M-Mix
3. MelGen
4. DrumGen
5. Xoxolo
6. Cadence
7. ArpGen
8. Counterpointer
9. Sidecar
10. Gremlin
11. GremlinDriver
12. Ground
13. Luma
14. Lifeform

Small correction: the DAW itself didnt crash - it just failed to load the plugins and gave a notification about it "The plugin failed to load"

## Investigation notes

The exact Windows artifact from the
[v0.14.1 release](https://github.com/danja/downspout/releases/tag/v0.14.1)
was downloaded and checked. Its SHA-256 is
`3ff0ea4c38b209ab50ac755232de5979c04890c4698e7ef6c56bffe0ab0f8666`,
matching the digest published by GitHub.

The archive contains 24 VST3 bundles in the expected
`<plugin>.vst3/Contents/x86_64-win/<plugin>.vst3` layout. Sidecar is not
included because it is currently Linux-only. If Sidecar appears in a Windows
Ableton scan, it came from an older or separately installed build.

### Steinberg validator results

Steinberg's VST3 validator was run against all 25 local Linux bundles. A
Windows validator was also cross-built with MinGW, passed all 43 of its own
self-tests under Wine, and was then run against all 24 bundles from the exact
v0.14.1 Windows release.

The native and Wine results agree:

- all plugins reported as failing in Ableton pass all 47 validator tests;
- Ambo and Basilico each pass 46 of 47 tests because their bypass parameter is
  not synchronized in the controller;
- those two validator failures do not correlate with the Ableton report,
  because Ambo and Basilico were not among the plugins that failed to load;
- the Windows binaries have the expected `GetPluginFactory`, `InitDll`, and
  `ExitDll` exports;
- all component and controller class IDs are unique;
- the bundles depend only on normal Windows system libraries, with no missing
  MinGW runtime dependency;
- the DPF assertion messages produced during validation occur in both
  Ableton-working and Ableton-failing plugins and are not a useful classifier
  for this report.

The bundles do not contain `moduleinfo.json`. That is consistent across the
whole release, and Steinberg's validator accepts the bundle structure. Factory
metadata is readable and consistent, although the plugin version is currently
reported as `0.1.0`, the SDK version as `VST 3.7.4`, and the vendor email is
empty. These metadata details do not divide the working and failing groups.

Steinberg documents the validator as the SDK's command-line conformance host:
[Validator command line](https://steinbergmedia.github.io/vst3_dev_portal/pages/What%2Bis%2Bthe%2BVST%2B3%2BSDK/Validator.html).

### Systematic Ableton pattern

All twelve event-only plugins in the Windows release had zero audio outputs,
and every one is in the Ableton failure list:

- BassGen, M-Mix, MelGen, and DrumGen;
- Xoxolo, Cadence, ArpGen, and Counterpointer;
- GremlinDriver, Ground, Luma, and Lifeform.

This closely matches a historical DPF issue,
[VST3: Ableton Live recognizes plugins but does not load them](https://github.com/DISTRHO/DPF/issues/372).
That investigation found that DPF VST3 plugins with
`DISTRHO_PLUGIN_NUM_OUTPUTS 0` could load in Reaper, Bitwig, and Carla while
failing in Ableton. Adding conventional audio buses was part of the confirmed
Ableton compatibility workaround.

Ableton documents support for routing MIDI produced by a VST plugin, but that
does not establish that a pure event-only VST3 with no audio output bus is a
supported device shape:
[Accessing the MIDI output of a VST plug-in](https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in).

The leading explanation is therefore an Ableton/DPF compatibility problem with
zero-audio-output VST3 bundles, rather than corrupt release binaries or a
general VST3 conformance failure.

Two entries need separate treatment:

- Gremlin has a stereo audio output and passes validation, so it does not fit
  the event-only pattern.
- Sidecar is absent from the Windows v0.14.1 archive, indicating that Ableton
  found another installed copy.

### Recommended next checks

1. Remove all older Downspout bundles, install only the v0.14.1 Windows
   package, and perform Ableton's full `Alt`-Rescan. Sidecar should no longer
   appear.
2. Retest Gremlin after the clean scan. If it still fails, collect Ableton's
   plugin-scanner log and test its editor in Steinberg's Windows
   VST3PluginTestHost; the command-line validator does not fully exercise
   opening the custom UI.
3. Test a Windows build containing the silent-output rollout described below.
   If the event-only plugins now load, it confirms the zero-audio-bus
   compatibility problem. If they still fail, collect Ableton's plugin-scanner
   log before changing the wrappers further.

### Silent-output compatibility rollout

The BassGen experiment passed its Linux smoke check, so the same plugin-local
wrapper change was applied to all remaining event-only plugins: M-Mix, MelGen,
DrumGen, Xoxolo, Cadence, ArpGen, Counterpointer, GremlinDriver, Ground, Luma,
Lifeform, and the Linux-only Sidecar. Each wrapper advertises two silent audio
outputs as one stereo bus and clears both buffers on every process block. The
portable cores and MIDI input/output behavior remain unchanged.

All thirteen affected plugins build natively on Linux. The twelve plugins
included in Windows releases also cross-build for Windows x86_64. Steinberg's
native validator reports the following for all thirteen, and its Windows
validator under Wine reports the same for all twelve Windows targets:

- zero audio input buses and one active stereo audio output bus;
- the plugins' existing MIDI input/output bus topology unchanged;
- 47 tests passed and zero tests failed.

All fourteen focused core suites also pass; that count includes P-Mix because
its cosmetic header was rebuilt and tested alongside the event-only plugins.

The twelve rebuilt Windows bundles are packaged for third-party Ableton testing
at
`build/bassgen-win-experiment/downspout-ableton-silent-output-windows-x86_64.zip`
(SHA-256
`29fcb64e5521166bb0cc0e4b81f6bc10afcc6bc7a4c27442391c314b56aee1c8`).

The remaining decisive check is external because the Ableton report came from
a third party and no local Ableton installation is available: install a
Windows package containing this rollout, perform a full rescan, and confirm
whether the twelve event-only plugins now load in Ableton Live on Windows 11.

### Header cleanup

BassGen, DrumGen, M-Mix, and P-Mix no longer show format labels styled like
buttons in their headers. Their subtitle now shows plugin version `v0.1.0`.
For each of these plugins, the UI string and VST3 `getVersion()` value are
driven by the same plugin-local version constants so they cannot drift
independently. This is the plugin metadata version, not the repository release
tag.
