#include "downspout/dpf/MagnetoKit.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

// Must match DamianoPlugin.cpp exactly — indices are stable across saves
enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamDrive,
    kParamTone,
    kParamFoldCount,
    kParamMix,
    kParamOutputGain,
    kParamCCDrive,
    kParamCCChannel,
    kParamStereo,
    kParamDriveR,
    kParamModeR,
    kParamToneR,
    kParamFoldCountR,
    kParamCCDriveR,
    kParamCCShape,
    kParamCCShapeR,
    kParameterCount
};

constexpr std::array<const char*, 6> kModeNames = {{"Soft", "Tanh", "Fuzz", "Overdrive", "Tube", "Wavefold"}};
constexpr int kWavefold = 5;

// Values travel as state keys (the plugin has no automatable parameters), so the
// UI needs each parameter's state key, range and default.
struct ParamSpec {
    const char* key;
    float minimum, maximum, defaultValue;
    bool integer;
};

constexpr std::array<ParamSpec, kParameterCount> kSpecs = {{
    {"mode",         0.0f,   5.0f,   1.0f, true },
    {"drive",        1.0f,  10.0f,   2.0f, false},
    {"tone",         0.0f, 100.0f,  50.0f, false},
    {"fold_count",   1.0f,   8.0f,   2.0f, true },
    {"mix",          0.0f, 100.0f, 100.0f, false},
    {"output_gain", -24.0f, 24.0f,   0.0f, false},
    {"cc_drive",     0.0f, 127.0f,   0.0f, true },
    {"cc_channel",   1.0f,  16.0f,   1.0f, true },
    {"stereo",       0.0f,   1.0f,   0.0f, true },
    {"drive_r",      1.0f,  10.0f,   2.0f, false},
    {"mode_r",       0.0f,   5.0f,   1.0f, true },
    {"tone_r",       0.0f, 100.0f,  50.0f, false},
    {"fold_count_r", 1.0f,   8.0f,   2.0f, true },
    {"cc_drive_r",   0.0f, 127.0f,   0.0f, true },
    {"cc_shape",     0.0f, 127.0f,   0.0f, true },
    {"cc_shape_r",   0.0f, 127.0f,   0.0f, true },
}};

struct Tables {
    std::array<kit::Range, kParameterCount> ranges {};
    std::array<float, kParameterCount> defaults {};
    Tables()
    {
        for (std::size_t i = 0; i < kParameterCount; ++i) {
            ranges[i]   = {kSpecs[i].minimum, kSpecs[i].maximum, kSpecs[i].integer};
            defaults[i] = kSpecs[i].defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

constexpr kit::Accent kLeftAccent {198, 120, 64};    // copper
constexpr kit::Accent kRightAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kOutputAccent {198, 132, 58};  // amber

// Button ids: 0-5 pick the left (or both) mode, 10-15 the right mode.
constexpr int kActionModeLeft  = 0;
constexpr int kActionModeRight = 10;

}  // namespace

class DamianoUI : public MagnetoUI
{
public:
    DamianoUI() : MagnetoUI(tables().ranges.data(), kParameterCount, tables().defaults.data()) {}

protected:
    // Values arrive through stateChanged; ignoring host parameter notifications
    // keeps REAPER automation replay from overriding what the panel shows.
    void parameterChanged(uint32_t /*index*/, float /*value*/) override {}

    void stateChanged(const char* key, const char* value) override
    {
        if (key == nullptr || value == nullptr)
            return;
        for (std::uint32_t i = 0; i < kParameterCount; ++i) {
            if (std::strcmp(key, kSpecs[i].key) == 0) {
                setValue(i, static_cast<float>(std::atof(value)));
                return;
            }
        }
    }

    void sendValue(const uint32_t parameter, const float v) override
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(v));
        setState(kSpecs[parameter].key, buffer);
    }

    void onAction(const int id) override
    {
        if (id >= kActionModeRight)
            commit(kParamModeR, static_cast<float>(id - kActionModeRight));
        else
            commit(kParamMode, static_cast<float>(id - kActionModeLeft));
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("DAMIANO", "stereo distortion",
                   "Linked: both channels use the left settings. Split: each ear gets its own mode, drive and tone. "
                   "CC numbers are read on the CC channel; 0 = off.");

        const float width = static_cast<float>(getWidth());
        const float full = width - 48.0f;

        // Stereo, output and CC channel.
        const Rect top {24.0f, 108.0f, full, 92.0f};
        drawPanel(top, "STEREO AND OUTPUT", kOutputAccent);
        static constexpr const char* kStereoNames[2] = {"Linked", "Split"};
        label(top.x + 14.0f, top.y + 40.0f, "Stereo");
        drawSegments(kParamStereo, kStereoNames, 2, {top.x + 14.0f, top.y + 56.0f, 190.0f, 26.0f}, kOutputAccent);
        const float colW = (full - 220.0f - 2.0f * 24.0f - 14.0f) / 3.0f;
        float x = top.x + 220.0f;
        drawSlider(kParamMix, "Mix", format(kParamMix).c_str(), {x, top.y + 44.0f, colW, 30.0f}, kOutputAccent);
        x += colW + 24.0f;
        drawSlider(kParamOutputGain, "Output Gain", format(kParamOutputGain).c_str(), {x, top.y + 44.0f, colW, 30.0f},
                   kOutputAccent);
        x += colW + 24.0f;
        drawSlider(kParamCCChannel, "CC Channel", format(kParamCCChannel).c_str(), {x, top.y + 44.0f, colW, 30.0f},
                   kOutputAccent);

        const bool split = intValue(kParamStereo) == 1;
        const float panelW = (full - 12.0f) * 0.5f;
        channelPanel({24.0f, 212.0f, panelW, 340.0f}, split ? "LEFT CHANNEL" : "BOTH CHANNELS", kLeftAccent, false,
                     false);
        channelPanel({24.0f + panelW + 12.0f, 212.0f, panelW, 340.0f}, split ? "RIGHT CHANNEL" : "RIGHT (follows left)",
                     kRightAccent, true, !split);
    }

private:
    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[32];
        const float v = value(parameter);
        switch (parameter) {
        case kParamOutputGain:
            std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
            break;
        case kParamMix:
        case kParamTone:
        case kParamToneR:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v));
            break;
        case kParamDrive:
        case kParamDriveR:
            std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v));
            break;
        case kParamCCDrive:
        case kParamCCDriveR:
        case kParamCCShape:
        case kParamCCShapeR:
            if (intValue(parameter) == 0)
                return "off";
            std::snprintf(buffer, sizeof(buffer), "CC %d", intValue(parameter));
            break;
        default:
            std::snprintf(buffer, sizeof(buffer), "%d", intValue(parameter));
            break;
        }
        return buffer;
    }

    // One channel's controls. `right` selects the right-channel parameters; `dim`
    // fades the panel while Linked, when the right settings are not in use.
    void channelPanel(const Rect b, const char* title, const Accent accent, const bool right, const bool dim)
    {
        const uint32_t pMode  = right ? kParamModeR : kParamMode;
        const uint32_t pDrive = right ? kParamDriveR : kParamDrive;
        const uint32_t pTone  = right ? kParamToneR : kParamTone;
        const uint32_t pFolds = right ? kParamFoldCountR : kParamFoldCount;
        const uint32_t pCcDrive = right ? kParamCCDriveR : kParamCCDrive;
        const uint32_t pCcShape = right ? kParamCCShapeR : kParamCCShape;

        // The frame is drawn undimmed (its header is two overlapping fills, which would show
        // a seam when faded) in a muted accent while inactive; only the controls fade.
        const Accent shown = dim ? Accent {88, 100, 108} : accent;
        drawPanel(b, title, shown);
        setDim(dim);

        // Mode: three buttons per row.
        const int base = right ? kActionModeRight : kActionModeLeft;
        const float gap = 8.0f;
        const float bw = (b.w - 24.0f - 2.0f * gap) / 3.0f;
        for (int m = 0; m < 6; ++m) {
            const Rect r {b.x + 12.0f + static_cast<float>(m % 3) * (bw + gap),
                          b.y + 44.0f + static_cast<float>(m / 3) * 34.0f, bw, 28.0f};
            drawButton(base + m, kModeNames[static_cast<std::size_t>(m)], r, accent, m == intValue(pMode));
        }

        float y = b.y + 124.0f;
        const float sw = b.w - 24.0f;
        drawSlider(pDrive, "Drive", format(pDrive).c_str(), {b.x + 12.0f, y, sw, 30.0f}, accent);
        y += 44.0f;
        drawSlider(pTone, "Tone", format(pTone).c_str(), {b.x + 12.0f, y, sw, 30.0f}, accent);
        y += 44.0f;
        setDim(dim || intValue(pMode) != kWavefold);
        drawSlider(pFolds, "Folds (Wavefold)", format(pFolds).c_str(), {b.x + 12.0f, y, sw, 30.0f}, accent);
        setDim(dim);
        y += 44.0f;
        drawSlider(pCcDrive, "CC Drive", format(pCcDrive).c_str(), {b.x + 12.0f, y, sw, 30.0f}, accent);
        y += 44.0f;
        drawSlider(pCcShape, "CC Shape (picks the mode)", format(pCcShape).c_str(), {b.x + 12.0f, y, sw, 30.0f}, accent);
        setDim(false);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DamianoUI)
};

UI* createUI()
{
    return new DamianoUI();
}

END_NAMESPACE_DISTRHO
