#include "downspout/dpf/MagnetoKit.hpp"

#include "splay_params.hpp"

#include <array>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::splay;

constexpr kit::Accent kMatrixAccent {198, 132, 58};   // amber: where each input goes
constexpr kit::Accent kImageAccent {92, 140, 156};    // steel: width and inter-channel time
constexpr kit::Accent kOutputAccent {120, 140, 92};   // olive: final level and bypass

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};

    Tables()
    {
        for (std::size_t i = 0; i < core::kParameterSpecs.size(); ++i) {
            const core::ParamSpec& spec = core::kParameterSpecs[i];
            ranges[i] = {spec.minimum, spec.maximum, spec.integer};
            defaults[i] = spec.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

}  // namespace

class SplayUI : public MagnetoUI {
public:
    SplayUI()
        : MagnetoUI(tables().ranges.data(), static_cast<std::uint32_t>(core::kParameterCount),
                    tables().defaults.data())
    {
    }

protected:
    void onAction(const int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("SPLAY", "Stereo matrix & image shaper",
                   "Route each input into either output, then shape width and inter-channel time.");

        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawMatrixPanel({24.0f, 108.0f, full, 180.0f});
        drawImagePanel({24.0f, 300.0f, full, 180.0f});
        drawOutputPanel({24.0f, 492.0f, full, 180.0f});
    }

private:
    void drawMatrixPanel(const Rect b)
    {
        drawPanel(b, "MATRIX", kMatrixAccent, "Source rows, destination columns");
        drawSlider(static_cast<std::uint32_t>(core::ParamId::matrixLL), "L to L",
                   format(core::ParamId::matrixLL).c_str(), {b.x + 16.0f, b.y + 40.0f, 410.0f, 34.0f},
                   kMatrixAccent);
        drawSlider(static_cast<std::uint32_t>(core::ParamId::matrixLR), "L to R",
                   format(core::ParamId::matrixLR).c_str(), {b.x + 446.0f, b.y + 40.0f, 410.0f, 34.0f},
                   kMatrixAccent);
        drawSlider(static_cast<std::uint32_t>(core::ParamId::matrixRL), "R to L",
                   format(core::ParamId::matrixRL).c_str(), {b.x + 16.0f, b.y + 98.0f, 410.0f, 34.0f},
                   kMatrixAccent);
        drawSlider(static_cast<std::uint32_t>(core::ParamId::matrixRR), "R to R",
                   format(core::ParamId::matrixRR).c_str(), {b.x + 446.0f, b.y + 98.0f, 410.0f, 34.0f},
                   kMatrixAccent);
        label(b.x + 16.0f, b.y + 148.0f, "100% = unity gain, 0% = off. Defaults pass each input to its own output.",
              true, 11.0f);
    }

    void drawImagePanel(const Rect b)
    {
        drawPanel(b, "IMAGE", kImageAccent, "Mono to wide, plus Haas delay");
        drawSlider(static_cast<std::uint32_t>(core::ParamId::width), "Width",
                   format(core::ParamId::width).c_str(), {b.x + 16.0f, b.y + 40.0f, 260.0f, 34.0f},
                   kImageAccent);
        drawSlider(static_cast<std::uint32_t>(core::ParamId::imageDelay), "Image Delay",
                   format(core::ParamId::imageDelay).c_str(), {b.x + 296.0f, b.y + 40.0f, 260.0f, 34.0f},
                   kImageAccent);
        label(b.x + 592.0f, b.y + 38.0f, "Invert R", false, 12.0f);
        drawSegments(static_cast<std::uint32_t>(core::ParamId::invertR), core::kOffOnNames.data(), 2,
                     {b.x + 592.0f, b.y + 56.0f, 264.0f, 28.0f}, kImageAccent);

        label(b.x + 16.0f, b.y + 106.0f,
              "Width 0% folds to mono, 100% is unchanged, 200% doubles the side signal (M +/- S).", true, 11.0f);
        label(b.x + 16.0f, b.y + 128.0f,
              "Image Delay offsets the right channel by up to 20 ms; 0 ms leaves both channels aligned.", true, 11.0f);
        label(b.x + 16.0f, b.y + 150.0f,
              "Invert R flips that channel's phase: a classic trick for widening a mono-compatible source.", true, 11.0f);
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "OUTPUT", kOutputAccent, "2 in / 2 out, no plugin state");
        drawSlider(static_cast<std::uint32_t>(core::ParamId::level), "Output",
                   format(core::ParamId::level).c_str(), {b.x + 16.0f, b.y + 40.0f, 400.0f, 34.0f},
                   kOutputAccent);
        label(b.x + 452.0f, b.y + 38.0f, "Bypass", false, 12.0f);
        drawSegments(static_cast<std::uint32_t>(core::ParamId::bypass), core::kOffOnNames.data(), 2,
                     {b.x + 452.0f, b.y + 56.0f, 260.0f, 28.0f}, kOutputAccent);

        label(b.x + 16.0f, b.y + 106.0f, "SIGNAL FLOW", true, 11.0f);
        label(b.x + 16.0f, b.y + 128.0f, "in L/R  ->  matrix  ->  mid/side width  ->  delay R  ->  invert  ->  level  ->  out L/R",
              false, 12.0f);
        label(b.x + 16.0f, b.y + 152.0f,
              "Bypass crossfades to the untouched input. Every control is automatable; no MIDI and no transport.",
              true, 11.0f);
    }

    [[nodiscard]] std::string format(const core::ParamId id) const
    {
        char buffer[40];
        const float v = value(static_cast<std::uint32_t>(id));
        switch (id) {
        case core::ParamId::matrixLL:
        case core::ParamId::matrixLR:
        case core::ParamId::matrixRL:
        case core::ParamId::matrixRR:
        case core::ParamId::width:
        case core::ParamId::level:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", v * 100.0f);
            break;
        case core::ParamId::imageDelay:
            std::snprintf(buffer, sizeof(buffer), "%.1f ms", v);
            break;
        case core::ParamId::invertR:
        case core::ParamId::bypass:
            return core::kOffOnNames[core::clampInt(v, 0, 1)];
        case core::ParamId::count: break;
        }
        return buffer;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplayUI)
};

UI* createUI()
{
    return new SplayUI();
}

END_NAMESPACE_DISTRHO
