# gremlin audit

## Source

- LV2 source: `/home/danny/github/flues/lv2/gremlin`
- main shell: `src/gremlin_plugin.cpp`
- engine: `src/GremlinEngine.hpp`
- original X11 UI: `src/ui/gremlin_ui_x11.c`

## Portable core identified

The audio engine already lives in a reusable header with clear setters for the
live and hidden controls. The main LV2 plugin file mostly handled:

- port plumbing
- MIDImix mapping
- scene loading
- macro/momentary application
- action triggers

Those behaviors were moved into a portable `gremlin_processor` layer in
`downspout`.

## Main VST3 assumptions

- collapse note and controller input into one MIDI input stream
- do not reproduce MIDImix LED output in the first VST3 pass
- keep live effective-value calculation in the processor, not the UI
- expose processor status through output parameters so the UI can mirror the
  effective state
