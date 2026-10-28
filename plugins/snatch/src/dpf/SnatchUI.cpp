#include "downspout/dpf/MagnetoKit.hpp"

#include "snatch_core_types.hpp"
#include "snatch_params.hpp"

#include <array>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

// Same order as ParamId in SnatchPlugin.cpp.
enum Param : std::uint32_t { kEngine = 0, kPreset, kPitch, kTone, kNoise, kSnap, kBody, kDecay, kVolume, kDistortion, kNoteMap, kCount };

constexpr kit::Accent kSourceAccent {198, 120, 64};  // copper
constexpr kit::Accent kShapeAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kOutAccent {120, 132, 88};     // olive

const char* const kEngineNames[6] = {"Tone + Noise", "Metal", "Brush", "Electronic", "Acoustic", "Side Stick"};
const char* const kNoteMapNames[2] = {"Any note plays the selected engine", "General MIDI: 37 stick, 38 snare, 40 electric"};
const char* const kPresetNames[5] = {"Rock", "Jazz", "Electronic", "Latin", "Brushed"};

struct Tables {
    std::array<kit::Range, kCount> ranges {};
    std::array<float, kCount> defaults {};
    Tables()
    {
        ranges[kEngine] = {0, 5, true};      defaults[kEngine] = 0;
        ranges[kPreset] = {0, 4, true};      defaults[kPreset] = 0;
        ranges[kPitch] = {100, 400, false};  defaults[kPitch] = 200;
        ranges[kTone] = {0, 1, false};       defaults[kTone] = 0.5f;
        ranges[kNoise] = {0, 1, false};      defaults[kNoise] = 0.7f;
        ranges[kSnap] = {0, 1, false};       defaults[kSnap] = 0.6f;
        ranges[kBody] = {0, 1, false};       defaults[kBody] = 0.6f;
        ranges[kDecay] = {0.05f, 1, false};  defaults[kDecay] = 0.25f;
        ranges[kVolume] = {0, 1, false};     defaults[kVolume] = 0.8f;
        ranges[kDistortion] = {0, 1, false}; defaults[kDistortion] = 0;
        ranges[kNoteMap] = {0, 1, true};     defaults[kNoteMap] = 1;
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

}  // namespace

class SnatchUI : public MagnetoUI
{
public:
    SnatchUI() : MagnetoUI(tables().ranges.data(), kCount, tables().defaults.data()) {}

protected:
    void onAction(const int /*id*/) override {}

    // A preset is a one-shot: it sends its sound values to the host, then they are ordinary parameters.
    void sendValue(const std::uint32_t parameter, const float v) override
    {
        MagnetoUI::sendValue(parameter, v);
        if (parameter != kPreset)
            return;
        snatch::Params p;
        p.preset = static_cast<int>(v);
        snatch::SnatchEngine engine;
        engine.setParams(p);
        engine.applyPreset();
        const snatch::Params& r = engine.params();
        commit(kEngine, static_cast<float>(r.engine));
        commit(kPitch, r.pitch);
        commit(kNoise, r.noise);
        commit(kSnap, r.snap);
        commit(kDecay, r.decay);
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("SNATCH", "snare drum generator",
                   "MIDI notes fire one hit, any note or General MIDI snare notes (see Note Map); velocity sets level. "
                   "Pick an engine, then balance shell, wires and snap.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        const float x = 24.0f + 14.0f, w = full - 28.0f;

        drawPanel({24.0f, 108.0f, full, 138.0f}, "SOURCE", kSourceAccent, "a preset sets engine, pitch, noise, snap and decay once");
        label(x, 148.0f, "Engine");
        drawSegments(kEngine, kEngineNames, 6, {x, 166.0f, w, 28.0f}, kSourceAccent);
        label(x, 200.0f, "Preset");
        drawSegments(kPreset, kPresetNames, 5, {x, 216.0f, w, 24.0f}, kSourceAccent);

        const Rect shape {24.0f, 256.0f, full, 140.0f};
        drawPanel(shape, "SHELL AND WIRES", kShapeAccent, "Tone only affects the Tone + Noise engine");
        slider(shape, 0, 0, kPitch, "Pitch (shell)", kShapeAccent);
        setDim(intValue(kEngine) != 0);
        slider(shape, 1, 0, kTone, "Tone (shell level)", kShapeAccent);
        setDim(false);
        slider(shape, 2, 0, kNoise, "Noise (wires)", kShapeAccent);
        slider(shape, 0, 1, kSnap, "Snap (attack burst)", kShapeAccent);
        slider(shape, 1, 1, kBody, "Body", kShapeAccent);
        slider(shape, 2, 1, kDecay, "Decay", kShapeAccent);

        const Rect out {24.0f, 406.0f, full, 140.0f};
        drawPanel(out, "OUTPUT AND MIDI", kOutAccent, "Side Stick is a short click: Decay is capped at 0.05 s");
        slider(out, 0, 0, kDistortion, "Distortion", kOutAccent);
        slider(out, 1, 0, kVolume, "Volume", kOutAccent);
        label(x, out.y + 92.0f, "Note Map (unmapped notes are ignored in General MIDI mode)");
        drawSegments(kNoteMap, kNoteMapNames, 2, {x, out.y + 108.0f, w, 24.0f}, kOutAccent);
    }

private:
    void slider(const Rect b, const int column, const int row, const std::uint32_t parameter, const char* name, const Accent accent)
    {
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - 2.0f * gap) / 3.0f;
        drawSlider(parameter, name, format(parameter).c_str(),
                   {b.x + 14.0f + static_cast<float>(column) * (w + gap), b.y + 44.0f + 48.0f * static_cast<float>(row), w, 34.0f}, accent);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[32];
        const double v = static_cast<double>(value(parameter));
        if (parameter == kPitch)
            std::snprintf(buffer, sizeof(buffer), "%.0f Hz", v);
        else if (parameter == kDecay)
            std::snprintf(buffer, sizeof(buffer), "%.2f s", v);
        else
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", v * 100.0);
        return buffer;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SnatchUI)
};

UI* createUI()
{
    return new SnatchUI();
}

END_NAMESPACE_DISTRHO
