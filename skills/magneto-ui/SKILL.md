---
name: magneto-ui
description: Build or change a Downspout plugin UI with the shared MagnetoKit (NanoVG) look and feel, then capture and review the screenshot. Use for any new UI, control layout change, or screenshot refresh.
---

# Magneto-style UI

All new plugins share the look of `plugins/magneto`. The kit is `include/downspout/dpf/MagnetoKit.hpp` (shared code: do not edit it without approval; after any approved change build `-vst3` for every plugin that includes it: `grep -rl MagnetoKit.hpp plugins`).

## Shape of a UI
- `class <Name>UI : public MagnetoUI`, constructed with a table of `kit::Range {min, max, isInt}` per parameter plus defaults. Parameter order must equal the enum in `<Name>Plugin.cpp`.
- Override `onAction(int)` (buttons) and `onNanoDisplay()`: `beginFrame(); drawHeader(title, subtitle, explainer);` then `drawPanel(rect, "TITLE", accent, "right caption")` blocks.
- Widgets: `drawSlider`, `drawStepper`, `drawSegments` (segment selector), `drawButton`, `drawPlotFrame`, `label`. Read values with `value()` / `intValue()`; write with `commit()`.
- Kit limits: 32 sliders, 16 actions, 8 steppers, 48 segments, 64 mirrored values per UI. Clatter needed its own `onMouse` for a 31-button grid; override the mouse handler rather than raising the limits.
- Use `setDim(true)` to grey controls that currently do nothing (e.g. FM controls when no FM engine is selected); reset with `setDim(false)`.
- Window size is `DISTRHO_UI_DEFAULT_WIDTH/HEIGHT` in `DistrhoPluginInfo.h`; panels are laid out in absolute rects.

## Patterns that worked
- **Presets are one-shots in the UI**: override `sendValue`, and when the preset parameter changes, `commit()` the resulting sound parameters. Do not apply presets in DSP: that overwrote saved parameters on project reload. A "Custom" preset changes nothing.
- Stepper/segment label tables are generated from the value and the index is clamped; an empty state shows no mark.
- Format values with units (Hz, st, s, %, x); show modes and routing contracts on screen (e.g. Note Map: "GM kick 35 / 36" vs "Any note").
- One accent colour per panel; the header explainer states what MIDI/audio does what.

## Screenshot loop (required for every UI change)
1. Ensure the plugin is listed in `scripts/capture-plugin-screenshots.sh`.
2. `scripts/capture-plugin-screenshots.sh --output-dir <scratchpad> <plugin>` (always uses a private Xvfb).
3. `identify` the PNG against the default width/height. A blank grey image: raise `DOWNSPOUT_SCREENSHOT_SETTLE_SECONDS`.
4. Open the PNG at full resolution and review as a first-time user: purpose and signal flow obvious; names, values, units, grouping legible; nothing clipped, crowded or ambiguous; defaults communicate useful behaviour. Iterate and recapture.
5. Only then copy to `docs/pages/assets/plugins/<plugin>.png`. A closed dropdown does not change the image, so no recapture is needed for list-only changes.
- Build `<plugin>-vst3` (not just the plain target) and check the exit status. Include everything the UI file uses (`<string>` etc.): the screenshot build can hide a missing include.
- If capture is unavailable, state the exact limitation and leave visual acceptance pending.
