#include "downspout/dpf/MagnetoKit.hpp"

#include "heavyverb_core.hpp"
#include "heavyverb_params.hpp"
#include "heavyverb_presets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::heavyverb;

namespace {

constexpr kit::Accent kSpaceAccent {86, 124, 152};     // slate blue
constexpr kit::Accent kDimensionAccent {116, 102, 164};  // dusk violet
constexpr kit::Accent kReverbAccent {92, 140, 156};    // cold steel
constexpr kit::Accent kEffectAccent {198, 132, 58};    // amber

constexpr std::array<const char*, core::kEffectCount> kEffectNames = {"Off", "Shimmer", "Swell", "Duck", "Gate"};

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::kParameterSpecs[i];
            ranges[i] = {s.minimum, s.maximum, s.integer, s.logarithmic};
            defaults[i] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

}  // namespace

class HeavyVerbUI : public MagnetoUI
{
public:
    HeavyVerbUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) {}

protected:
    // The only action is Randomise: a one-shot like a preset, applied as ordinary parameter values.
    void onAction(const int /*id*/) override
    {
        static std::random_device device;
        const core::Parameters current = currentParameters();
        const core::Parameters p = core::randomiseParameters(current, device());
        applying_ = true;
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            if (i != core::kPreset && i != core::kFreeze && core::field(p, i) != core::field(current, i))
                commit(i, core::field(p, i));
        applying_ = false;
        commit(core::kPreset, 0.0f);
    }

    // A preset is a one-shot: it sends its values to the host as ordinary parameters, which then belong to
    // the user again. Moving any other control marks the patch Custom. Preset 0 changes nothing.
    void sendValue(const std::uint32_t parameter, const float v) override
    {
        MagnetoUI::sendValue(parameter, v);
        if (parameter != core::kPreset) {
            if (!applying_ && parameter != core::kFreeze && intValue(core::kPreset) != 0)
                commit(core::kPreset, 0.0f);
            return;
        }
        const int number = static_cast<int>(std::lround(v));
        if (number < 1 || number > core::kPresetCount)
            return;
        const core::Parameters p = core::presetParameters(number);
        applying_ = true;
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            if (i != core::kPreset && i != core::kFreeze)
                commit(i, core::field(p, i));
        applying_ = false;
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("HEAVYVERB", "master-bus reverb",
                   "Each dimension is a pair of parallel walls: Distance sets how far apart, Damping how fast the highs "
                   "die, Filter what each reflection loses. The dry signal is never touched.");

        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawSpacePanel({24.0f, 108.0f, full, 84.0f});
        drawDimensionsPanel({24.0f, 204.0f, full, 318.0f});
        drawReverbPanel({24.0f, 534.0f, full, 150.0f});
        drawEffectsPanel({24.0f, 696.0f, full, 100.0f});
    }

private:
    [[nodiscard]] static std::string presetName(const int number)
    {
        if (number < 1 || number > core::kPresetCount) return "Custom";
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%d  %s", number, core::kPresets[static_cast<size_t>(number - 1)].name);
        return buffer;
    }

    void drawSpacePanel(const Rect b)
    {
        drawPanel(b, "SPACE", kSpaceAccent, "ten typical spaces (1-10), five special effects (11-15)");
        const int number = intValue(core::kPreset);
        drawStepper(core::kPreset, "Preset", presetName(number).c_str(), {b.x + 14.0f, b.y + 42.0f, 250.0f, 36.0f});
        const char* note = number >= 1 && number <= core::kPresetCount ? core::kPresets[static_cast<size_t>(number - 1)].note
                                                                        : "your own settings: moving a control after a preset lands here";
        label(b.x + 284.0f, b.y + 50.0f, note, true, 12.0f);
        char dims[32];
        std::snprintf(dims, sizeof(dims), "%d", intValue(core::kDimensions));
        drawStepper(core::kDimensions, "Dimensions", dims, {b.x + b.w - 14.0f - 150.0f, b.y + 42.0f, 150.0f, 36.0f});
    }

    void drawDimensionsPanel(const Rect b)
    {
        drawPanel(b, "DIMENSIONS", kDimensionAccent, "wall pair = two delay lines of length distance / 343 m/s");
        drawButton(1, "Randomise", {b.x + b.w - 440.0f, b.y + 5.0f, 130.0f, 24.0f}, kDimensionAccent, false);
        const int active = intValue(core::kDimensions);
        const float rowHeight = 46.0f;
        const float nameWidth = 64.0f, infoWidth = 140.0f, gap = 20.0f;
        const float sliderWidth = (b.w - 28.0f - nameWidth - infoWidth - 3.0f * gap) / 3.0f;
        for (int i = 0; i < core::kMaxDimensions; ++i) {
            const float y = b.y + 44.0f + static_cast<float>(i) * rowHeight;
            const bool on = i < active;
            setDim(!on);
            char name[16];
            std::snprintf(name, sizeof(name), "DIM %d", i + 1);
            label(b.x + 14.0f, y + 6.0f, name, false, 13.0f);
            float x = b.x + 14.0f + nameWidth;
            const std::uint32_t distance = core::kDistance1 + static_cast<std::uint32_t>(i);
            const std::uint32_t damping = core::kDamping1 + static_cast<std::uint32_t>(i);
            const std::uint32_t filter = core::kFilter1 + static_cast<std::uint32_t>(i);
            drawSlider(distance, "Distance", format(distance).c_str(), {x, y, sliderWidth, 34.0f}, kDimensionAccent);
            x += sliderWidth + gap;
            drawSlider(damping, "Damping", format(damping).c_str(), {x, y, sliderWidth, 34.0f}, kDimensionAccent);
            x += sliderWidth + gap;
            drawSlider(filter, "Filter (dark|thin)", format(filter).c_str(), {x, y, sliderWidth, 34.0f}, kDimensionAccent);
            x += sliderWidth + gap;
            const float metres = value(distance);
            char crossing[48], mode[48];
            std::snprintf(crossing, sizeof(crossing), "%.1f ms across", static_cast<double>(metres / core::kSpeedOfSound * 1000.0f));
            std::snprintf(mode, sizeof(mode), "axial mode %.0f Hz", static_cast<double>(core::kSpeedOfSound / (2.0f * metres)));
            label(x, y + 4.0f, crossing, true, 11.0f);
            label(x, y + 20.0f, mode, true, 11.0f);
            setDim(false);
        }
    }

    void drawReverbPanel(const Rect b)
    {
        drawPanel(b, "REVERB", kReverbAccent, "every dimension shares one Decay (RT60), so the tail stays even");
        const float gap = 20.0f;
        const float w = (b.w - 28.0f - 4.0f * gap) / 5.0f;
        const std::array<std::uint32_t, 5> top = {core::kDecay, core::kPreDelay, core::kMix, core::kWidth, core::kModulation};
        const std::array<std::uint32_t, 5> bottom = {core::kDiffusion, core::kEarly, core::kLowCut, core::kHighCut, core::kOutput};
        const std::array<const char*, 5> topNames = {"Decay (RT60)", "Pre-delay", "Mix (wet)", "Width", "Modulation"};
        const std::array<const char*, 5> bottomNames = {"Diffusion", "Early reflections", "Low cut (wet)", "High cut (wet)", "Output (wet)"};
        for (size_t k = 0; k < 5; ++k) {
            const float x = b.x + 14.0f + static_cast<float>(k) * (w + gap);
            drawSlider(top[k], topNames[k], format(top[k]).c_str(), {x, b.y + 44.0f, w, 34.0f}, kReverbAccent);
            drawSlider(bottom[k], bottomNames[k], format(bottom[k]).c_str(), {x, b.y + 98.0f, w, 34.0f}, kReverbAccent);
        }
    }

    void drawEffectsPanel(const Rect b)
    {
        const int effect = intValue(core::kEffect);
        drawPanel(b, "EFFECTS", kEffectAccent, "special effects shape the wet signal; Freeze holds whatever is ringing");
        const float y = b.y + 44.0f;
        drawStepper(core::kEffect, "Effect", kEffectNames[static_cast<size_t>(std::clamp(effect, 0, core::kEffectCount - 1))],
                    {b.x + 14.0f, y, 170.0f, 36.0f});

        const char* amountName = "Amount";
        switch (effect) {
        case core::kEffectShimmer: amountName = "Shimmer level"; break;
        case core::kEffectSwell:   amountName = "Swell fade-in time"; break;
        case core::kEffectDuck:    amountName = "Duck depth"; break;
        case core::kEffectGate:    amountName = "Gate hold time"; break;
        default: break;
        }
        setDim(effect == core::kEffectOff);
        drawSlider(core::kEffectAmount, amountName, format(core::kEffectAmount).c_str(), {b.x + 204.0f, y, 230.0f, 34.0f}, kEffectAccent);
        setDim(effect != core::kEffectShimmer);
        drawSlider(core::kShift, "Shimmer shift", format(core::kShift).c_str(), {b.x + 454.0f, y, 190.0f, 34.0f}, kEffectAccent);
        setDim(false);
        static const char* const kFreezeNames[] = {"Live", "Freeze"};
        label(b.x + 664.0f, y, "Freeze: holds the tail");
        drawSegments(core::kFreeze, kFreezeNames, 2, {b.x + 664.0f, y + 16.0f, b.w - 664.0f - 14.0f, 24.0f}, kEffectAccent);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[40];
        const float v = value(parameter);
        if (parameter >= core::kDistance1 && parameter <= core::kDistance6) {
            std::snprintf(buffer, sizeof(buffer), v >= 100.0f ? "%.0f m" : "%.1f m", static_cast<double>(v));
            return buffer;
        }
        if (parameter >= core::kFilter1 && parameter <= core::kFilter6) {
            if (std::fabs(v) < 0.5f) return "flat";
            std::snprintf(buffer, sizeof(buffer), v > 0.0f ? "thin %.0f%%" : "dark %.0f%%", static_cast<double>(std::fabs(v)));
            return buffer;
        }
        if (parameter >= core::kDamping1 && parameter <= core::kDamping6) {
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v));
            return buffer;
        }
        switch (parameter) {
        case core::kDecay:
            std::snprintf(buffer, sizeof(buffer), v < 10.0f ? "%.2f s" : "%.1f s", static_cast<double>(v));
            break;
        case core::kPreDelay:
            std::snprintf(buffer, sizeof(buffer), "%.0f ms", static_cast<double>(v));
            break;
        case core::kLowCut:
        case core::kHighCut:
            if (v >= 1000.0f)
                std::snprintf(buffer, sizeof(buffer), "%.1f kHz", static_cast<double>(v) / 1000.0);
            else
                std::snprintf(buffer, sizeof(buffer), "%.0f Hz", static_cast<double>(v));
            break;
        case core::kOutput:
            std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
            break;
        case core::kShift:
            std::snprintf(buffer, sizeof(buffer), "%+d st", intValue(parameter));
            break;
        case core::kEffectAmount: {
            const float a = v * 0.01f;
            switch (intValue(core::kEffect)) {
            case core::kEffectSwell: std::snprintf(buffer, sizeof(buffer), "%.1f s", static_cast<double>(0.1f + 2.4f * a)); break;
            case core::kEffectGate:  std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(0.05f + 0.95f * a)); break;
            case core::kEffectDuck:  std::snprintf(buffer, sizeof(buffer), "up to %.0f dB", static_cast<double>(24.0f * a)); break;
            default:                 std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v)); break;
            }
            break;
        }
        default:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v));
            break;
        }
        return buffer;
    }

    [[nodiscard]] core::Parameters currentParameters() const
    {
        core::Parameters p;
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            core::field(p, i) = value(i);
        return p;
    }

    bool applying_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeavyVerbUI)
};

UI* createUI()
{
    return new HeavyVerbUI();
}

END_NAMESPACE_DISTRHO
