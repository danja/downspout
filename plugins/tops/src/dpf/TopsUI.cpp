#include "downspout/dpf/MagnetoKit.hpp"

#include "tops_core_types.hpp"
#include "tops_params.hpp"

#include <array>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

// Same order as ParamId in TopsPlugin.cpp.
enum Param : std::uint32_t { kInstrument = 0, kEngine, kPreset, kPitch, kDecay, kTone, kNoise, kBrightness, kVolume, kDistortion, kNoteMap, kCount };

constexpr kit::Accent kSourceAccent {198, 120, 64};  // copper
constexpr kit::Accent kShapeAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kOutAccent {120, 132, 88};     // olive

const char* const kInstrumentNames[10] = {"Closed HH", "Open HH", "Pedal HH", "Crash", "Ride",
                                          "Splash", "China", "Cowbell", "Tambourine", "Triangle"};
const char* const kEngineNames[6] = {"Metallic", "Noise", "FM", "Physical", "Wavetable", "Ring Mod"};
const char* const kPresetNames[5] = {"Rock", "Jazz", "Electronic", "Metal", "Latin"};
const char* const kNoteMapNames[2] = {"Any note plays the selected instrument", "General MIDI drum notes"};

struct Tables {
    std::array<kit::Range, kCount> ranges {};
    std::array<float, kCount> defaults {};
    Tables()
    {
        ranges[kInstrument] = {0, 9, true};      defaults[kInstrument] = 0;
        ranges[kEngine] = {0, 5, true};          defaults[kEngine] = 0;
        ranges[kPreset] = {0, 4, true};          defaults[kPreset] = 0;
        ranges[kPitch] = {1000, 12000, false, true}; defaults[kPitch] = 8000;
        ranges[kDecay] = {0.01f, 2, false};      defaults[kDecay] = 0.08f;
        ranges[kTone] = {0, 1, false};           defaults[kTone] = 0.6f;
        ranges[kNoise] = {0, 1, false};          defaults[kNoise] = 0.5f;
        ranges[kBrightness] = {0, 1, false};     defaults[kBrightness] = 0.7f;
        ranges[kVolume] = {0, 1, false};         defaults[kVolume] = 0.7f;
        ranges[kDistortion] = {0, 1, false};     defaults[kDistortion] = 0;
        ranges[kNoteMap] = {0, 1, true};         defaults[kNoteMap] = 1;
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

}  // namespace

class TopsUI : public MagnetoUI
{
public:
    TopsUI() : MagnetoUI(tables().ranges.data(), kCount, tables().defaults.data()) {}

protected:
    void onAction(const int /*id*/) override {}

    // A preset is a one-shot: it sends its sound values to the host, then they are ordinary parameters.
    void sendValue(const std::uint32_t parameter, const float v) override
    {
        MagnetoUI::sendValue(parameter, v);
        if (parameter != kPreset)
            return;
        tops::Params p;
        p.preset = static_cast<int>(v);
        tops::TopsEngine engine;
        engine.setParams(p);
        engine.applyPreset();
        const tops::Params& r = engine.params();
        commit(kDecay, r.decay);
        commit(kBrightness, r.brightness);
        commit(kNoise, r.noise);
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("TOPS", "hi-hat and cymbal generator",
                   "MIDI notes play the instrument chosen here, or General MIDI drum notes (see Note Map); "
                   "velocity sets level. Closed and pedal hats choke an open hat.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        const float x = 24.0f + 14.0f, w = full - 28.0f;

        drawPanel({24.0f, 108.0f, full, 200.0f}, "SOURCE", kSourceAccent, "a preset sets decay, brightness and noise once");
        label(x, 148.0f, "Instrument");
        drawSegments(kInstrument, kInstrumentNames, 10, {x, 166.0f, w, 28.0f}, kSourceAccent);
        label(x, 204.0f, "Engine");
        drawSegments(kEngine, kEngineNames, 6, {x, 222.0f, w, 28.0f}, kSourceAccent);
        label(x, 260.0f, "Preset");
        drawSegments(kPreset, kPresetNames, 5, {x, 276.0f, w, 24.0f}, kSourceAccent);

        const Rect shape {24.0f, 318.0f, full, 140.0f};
        drawPanel(shape, "SOUND", kShapeAccent, "Decay is scaled per instrument; closed and pedal hats have a fixed short decay");
        slider(shape, 0, 0, kPitch, "Pitch", kShapeAccent);
        slider(shape, 1, 0, kDecay, "Decay", kShapeAccent);
        slider(shape, 2, 0, kNoise, "Noise (mix)", kShapeAccent);
        slider(shape, 0, 1, kBrightness, "Brightness (low-pass)", kShapeAccent);
        slider(shape, 1, 1, kTone, "Tone (highs up / down)", kShapeAccent);

        const Rect out {24.0f, 470.0f, full, 140.0f};
        drawPanel(out, "OUTPUT AND MIDI", kOutAccent, "GM notes: 42 closed, 44 pedal, 46 open, 49 crash, 51 ride, 55 splash, 52 china, 56 cowbell, 54 tamb, 81 triangle");
        slider(out, 0, 0, kDistortion, "Distortion", kOutAccent);
        slider(out, 1, 0, kVolume, "Volume", kOutAccent);
        label(x, out.y + 92.0f, "Note Map (unmapped notes are ignored in GM mode)");
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
            std::snprintf(buffer, sizeof(buffer), "%.2f kHz", v / 1000.0);
        else if (parameter == kDecay)
            std::snprintf(buffer, sizeof(buffer), "%.2f s", v);
        else
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", v * 100.0);
        return buffer;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopsUI)
};

UI* createUI()
{
    return new TopsUI();
}

END_NAMESPACE_DISTRHO
