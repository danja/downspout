#include "downspout/dpf/MagnetoKit.hpp"

#include "thumper_core_types.hpp"
#include "thumper_params.hpp"

#include <array>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

// Same order as ParamId in ThumperPlugin.cpp.
enum Param : std::uint32_t {
    kEngine = 0, kPreset, kPitch, kPitchDrop, kDecay, kClick, kBody, kDrive, kTone, kVolume,
    kFmRatio, kFmIndex, kModalDecay, kSubMix, kDistortion,
    kPitchEnv, kFilterEnv, kAmpEnv, kEngine2, kLayerMix, kNoteMap, kCount
};
constexpr int kCustomPreset = 7;

constexpr kit::Accent kSourceAccent {198, 120, 64};  // copper
constexpr kit::Accent kShapeAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kEnvAccent {198, 132, 58};     // amber
constexpr kit::Accent kOutAccent {120, 132, 88};     // olive

const char* const kEngineNames[10] = {"Analog", "Modal", "Subtract", "FM", "Phase Dist",
                                      "Wavetable", "808", "909", "Physical", "Noise"};
const char* const kNoteMapNames[2] = {"Any note", "GM kick 35 / 36"};
const char* const kPresetNames[8] = {"Techno", "Gabber", "Rock", "Latin", "House", "Trap", "Acoustic", "Custom"};

struct Tables {
    std::array<kit::Range, kCount> ranges {};
    std::array<float, kCount> defaults {};
    Tables()
    {
        ranges[kEngine] = {0, 9, true};          defaults[kEngine] = 0;
        ranges[kPreset] = {0, 7, true};          defaults[kPreset] = 0;
        ranges[kPitch] = {30, 120, false};       defaults[kPitch] = 55;
        ranges[kPitchDrop] = {0, 24, false};     defaults[kPitchDrop] = 12;
        ranges[kDecay] = {0.05f, 2, false};      defaults[kDecay] = 0.5f;
        ranges[kClick] = {0, 1, false};          defaults[kClick] = 0.5f;
        ranges[kBody] = {0, 1, false};           defaults[kBody] = 0.7f;
        ranges[kDrive] = {0, 1, false};          defaults[kDrive] = 0.3f;
        ranges[kTone] = {0, 1, false};           defaults[kTone] = 0.5f;
        ranges[kVolume] = {0, 1, false};         defaults[kVolume] = 0.8f;
        ranges[kFmRatio] = {0.5f, 8, false};     defaults[kFmRatio] = 2;
        ranges[kFmIndex] = {0, 10, false};       defaults[kFmIndex] = 3;
        ranges[kModalDecay] = {0.05f, 2, false}; defaults[kModalDecay] = 0.4f;
        ranges[kSubMix] = {0, 1, false};         defaults[kSubMix] = 0.5f;
        ranges[kDistortion] = {0, 1, false};     defaults[kDistortion] = 0;
        ranges[kPitchEnv] = {0.001f, 2, false};  defaults[kPitchEnv] = 0.1f;
        ranges[kFilterEnv] = {0.001f, 2, false}; defaults[kFilterEnv] = 0.3f;
        ranges[kAmpEnv] = {0.001f, 2, false};    defaults[kAmpEnv] = 0.5f;
        ranges[kEngine2] = {0, 9, true};         defaults[kEngine2] = 0;
        ranges[kLayerMix] = {0, 1, false};       defaults[kLayerMix] = 0;
        ranges[kNoteMap] = {0, 1, true};         defaults[kNoteMap] = 1;
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

}  // namespace

class ThumperUI : public MagnetoUI
{
public:
    ThumperUI() : MagnetoUI(tables().ranges.data(), kCount, tables().defaults.data()) {}

protected:
    void onAction(const int /*id*/) override {}

    // A preset is a one-shot: it sends its sound values to the host, then they are ordinary
    // parameters again. Custom (7) changes nothing.
    void sendValue(const std::uint32_t parameter, const float v) override
    {
        MagnetoUI::sendValue(parameter, v);
        if (parameter != kPreset || static_cast<int>(v) == kCustomPreset)
            return;
        thumper::Params p;
        p.preset = static_cast<int>(v);
        thumper::ThumperEngine engine;
        engine.setParams(p);
        engine.applyPreset();
        const thumper::Params& r = engine.params();
        commit(kEngine, static_cast<float>(r.engine));
        commit(kPitch, r.pitch);
        commit(kPitchDrop, r.pitchDrop);
        commit(kDecay, r.decay);
        commit(kClick, r.click);
        commit(kBody, r.body);
        commit(kDrive, r.drive);
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("THUMPER", "kick drum generator",
                   "Any MIDI note fires a kick, or only GM notes 35/36 (Note Map); velocity sets level. Channel 10 plays at Pitch, other channels at the note's pitch.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawSourcePanel({24.0f, 108.0f, full, 138.0f});
        drawShapePanel({24.0f, 256.0f, full, 140.0f});
        drawEnvPanel({24.0f, 406.0f, full, 96.0f});
        drawOutputPanel({24.0f, 512.0f, full, 150.0f});
    }

private:
    void drawSourcePanel(const Rect b)
    {
        drawPanel(b, "SOURCE", kSourceAccent, "a preset sets engine, pitch, drop, decay, click, body and drive once");
        const float x = b.x + 14.0f, w = b.w - 28.0f;
        label(x, b.y + 40.0f, "Engine");
        drawSegments(kEngine, kEngineNames, 10, {x, b.y + 58.0f, w, 28.0f}, kSourceAccent);
        const float presetW = w - 330.0f;
        label(x, b.y + 92.0f, "Preset");
        drawSegments(kPreset, kPresetNames, 8, {x, b.y + 108.0f, presetW, 24.0f}, kSourceAccent);
        label(x + presetW + 30.0f, b.y + 92.0f, "Note Map");
        drawSegments(kNoteMap, kNoteMapNames, 2, {x + presetW + 30.0f, b.y + 108.0f, 300.0f, 24.0f}, kSourceAccent);
    }

    void drawShapePanel(const Rect b)
    {
        drawPanel(b, "PITCH AND BODY", kShapeAccent);
        slider(b, 0, 0, kPitch, "Pitch (resting)", kShapeAccent, 4);
        slider(b, 1, 0, kPitchDrop, "Pitch Drop (start above rest)", kShapeAccent, 4);
        slider(b, 2, 0, kDecay, "Decay (length)", kShapeAccent, 4);
        slider(b, 3, 0, kTone, "Tone (filter)", kShapeAccent, 4);
        slider(b, 0, 1, kClick, "Click (noise attack)", kShapeAccent, 4);
        slider(b, 1, 1, kBody, "Body", kShapeAccent, 4);
        slider(b, 2, 1, kSubMix, "Sub (octave down)", kShapeAccent, 4);
        slider(b, 3, 1, kDrive, "Drive (saturation)", kShapeAccent, 4);
    }

    void drawEnvPanel(const Rect b)
    {
        drawPanel(b, "ENVELOPES", kEnvAccent, "time constants; Modal and FM controls apply to those engines");
        slider(b, 0, 0, kPitchEnv, "Pitch Env", kEnvAccent, 6);
        slider(b, 1, 0, kFilterEnv, "Filter Env", kEnvAccent, 6);
        slider(b, 2, 0, kAmpEnv, "Amp Env", kEnvAccent, 6);
        const bool layered = value(kLayerMix) > 0.0f;
        const auto uses = [&](const int engine) { return intValue(kEngine) == engine || (layered && intValue(kEngine2) == engine); };
        setDim(!uses(1));
        slider(b, 3, 0, kModalDecay, "Modal Decay", kEnvAccent, 6);
        setDim(!uses(3));
        slider(b, 4, 0, kFmRatio, "FM Ratio", kEnvAccent, 6);
        slider(b, 5, 0, kFmIndex, "FM Index", kEnvAccent, 6);
        setDim(false);
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "LAYER AND OUTPUT", kOutAccent, "Layer Mix 0 % = single engine");
        const float x = b.x + 14.0f, w = b.w - 28.0f;
        label(x, b.y + 40.0f, "Second engine (blended in by Layer Mix)");
        setDim(value(kLayerMix) <= 0.0f);
        drawSegments(kEngine2, kEngineNames, 10, {x, b.y + 58.0f, w, 28.0f}, kOutAccent);
        setDim(false);
        slider(b, 0, 2, kLayerMix, "Layer Mix", kOutAccent, 3, 104.0f);
        slider(b, 1, 2, kDistortion, "Output Distortion", kOutAccent, 3, 104.0f);
        slider(b, 2, 2, kVolume, "Volume", kOutAccent, 3, 104.0f);
    }

    // One slider in a `columns`-wide grid; rows are 48 px apart starting 44 px below the panel top.
    void slider(const Rect b, const int column, const int row, const std::uint32_t parameter, const char* name,
                const Accent accent, const int columns, const float yOffset = -1.0f)
    {
        const float gap = 20.0f;
        const float w = (b.w - 28.0f - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        const float y = b.y + (yOffset >= 0.0f ? yOffset : 44.0f + 48.0f * static_cast<float>(row));
        drawSlider(parameter, name, format(parameter).c_str(),
                   {b.x + 14.0f + static_cast<float>(column) * (w + gap), y, w, 34.0f}, accent);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[32];
        const float v = value(parameter);
        switch (parameter) {
        case kPitch: std::snprintf(buffer, sizeof(buffer), "%.0f Hz", static_cast<double>(v)); break;
        case kPitchDrop: std::snprintf(buffer, sizeof(buffer), "%.1f st", static_cast<double>(v)); break;
        case kDecay: case kPitchEnv: case kFilterEnv: case kAmpEnv: case kModalDecay:
            std::snprintf(buffer, sizeof(buffer), "%.3f s", static_cast<double>(v)); break;
        case kFmRatio: std::snprintf(buffer, sizeof(buffer), "x%.2f", static_cast<double>(v)); break;
        case kFmIndex: std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(v)); break;
        default: std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v) * 100.0); break;
        }
        return buffer;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThumperUI)
};

UI* createUI()
{
    return new ThumperUI();
}

END_NAMESPACE_DISTRHO
