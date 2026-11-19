#include "downspout/dpf/MagnetoKit.hpp"

#include "splay_params.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::splay;

constexpr kit::Accent kMatrixAccent {198, 132, 58};    // amber: routing and rotation
constexpr kit::Accent kImageAccent {92, 140, 156};     // steel: width and inter-channel time
constexpr kit::Accent kSpectralAccent {148, 112, 168}; // violet: two-band width
constexpr kit::Accent kOutputAccent {120, 140, 92};    // olive: level, bypass and meters

constexpr std::uint32_t pid(const core::ParamId id) noexcept
{
    return static_cast<std::uint32_t>(id);
}

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
    void onAction(const int id) override
    {
        if (id == 0) {
            // Swap destinations: the old column for each source becomes its new row.
            const float ll = value(pid(core::ParamId::matrixLL));
            const float lr = value(pid(core::ParamId::matrixLR));
            const float rl = value(pid(core::ParamId::matrixRL));
            const float rr = value(pid(core::ParamId::matrixRR));
            commit(pid(core::ParamId::matrixLL), rl);
            commit(pid(core::ParamId::matrixLR), rr);
            commit(pid(core::ParamId::matrixRL), ll);
            commit(pid(core::ParamId::matrixRR), lr);
        } else if (id == 1) {
            commit(pid(core::ParamId::matrixLL), 1.0f);
            commit(pid(core::ParamId::matrixLR), 0.0f);
            commit(pid(core::ParamId::matrixRL), 0.0f);
            commit(pid(core::ParamId::matrixRR), 1.0f);
        }
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("SPLAY", "Stereo matrix & image shaper",
                   "Matrix, image, spectral width and final output.");

        drawMatrixPanel({24.0f, 108.0f, 872.0f, 170.0f});
        drawImagePanel({24.0f, 290.0f, 872.0f, 130.0f});
        drawSpectralPanel({24.0f, 432.0f, 872.0f, 100.0f});
        drawOutputPanel({24.0f, 544.0f, 872.0f, 132.0f});
    }

private:
    void drawMatrixPanel(const Rect b)
    {
        drawPanel(b, "MATRIX", kMatrixAccent, "routing + rotation");
        drawSlider(pid(core::ParamId::matrixLL), "L to L",
                   format(core::ParamId::matrixLL).c_str(), {b.x + 16.0f, b.y + 40.0f, 224.0f, 34.0f},
                   kMatrixAccent);
        drawSlider(pid(core::ParamId::matrixLR), "L to R",
                   format(core::ParamId::matrixLR).c_str(), {b.x + 256.0f, b.y + 40.0f, 224.0f, 34.0f},
                   kMatrixAccent);
        drawSlider(pid(core::ParamId::rotation), "Rotation",
                   format(core::ParamId::rotation).c_str(), {b.x + 496.0f, b.y + 40.0f, 224.0f, 34.0f},
                   kMatrixAccent);
        drawButton(0, "Swap L/R", {b.x + 740.0f, b.y + 44.0f, 116.0f, 30.0f}, kMatrixAccent, false);

        drawSlider(pid(core::ParamId::matrixRL), "R to L",
                   format(core::ParamId::matrixRL).c_str(), {b.x + 16.0f, b.y + 98.0f, 224.0f, 34.0f},
                   kMatrixAccent);
        drawSlider(pid(core::ParamId::matrixRR), "R to R",
                   format(core::ParamId::matrixRR).c_str(), {b.x + 256.0f, b.y + 98.0f, 224.0f, 34.0f},
                   kMatrixAccent);
        drawSlider(pid(core::ParamId::panLaw), "Pan Law",
                   format(core::ParamId::panLaw).c_str(), {b.x + 496.0f, b.y + 98.0f, 224.0f, 34.0f},
                   kMatrixAccent);
        drawButton(1, "Reset", {b.x + 740.0f, b.y + 102.0f, 116.0f, 30.0f}, kMatrixAccent, false);
    }

    void drawImagePanel(const Rect b)
    {
        drawPanel(b, "IMAGE", kImageAccent, "width + mid/side");
        drawSlider(pid(core::ParamId::width), "Width", format(core::ParamId::width).c_str(),
                   {b.x + 16.0f, b.y + 40.0f, 224.0f, 34.0f}, kImageAccent);
        drawSlider(pid(core::ParamId::midGain), "Mid", format(core::ParamId::midGain).c_str(),
                   {b.x + 256.0f, b.y + 40.0f, 224.0f, 34.0f}, kImageAccent);
        drawSlider(pid(core::ParamId::imageDelay), "Image Delay",
                   format(core::ParamId::imageDelay).c_str(), {b.x + 496.0f, b.y + 40.0f, 224.0f, 34.0f},
                   kImageAccent);
        label(b.x + 740.0f, b.y + 40.0f, "Invert R", false, 12.0f);
        drawSegments(pid(core::ParamId::invertR), core::kOffOnNames.data(), 2,
                     {b.x + 740.0f, b.y + 58.0f, 116.0f, 26.0f}, kImageAccent);

        label(b.x + 16.0f, b.y + 88.0f, "M/S Solo", false, 12.0f);
        drawSegments(pid(core::ParamId::msSolo), core::kSoloNames.data(), 3,
                     {b.x + 16.0f, b.y + 106.0f, 280.0f, 24.0f}, kImageAccent);
    }

    void drawSpectralPanel(const Rect b)
    {
        drawPanel(b, "SPECTRAL", kSpectralAccent, "two-band width");
        drawSlider(pid(core::ParamId::crossover), "Crossover",
                   format(core::ParamId::crossover).c_str(), {b.x + 16.0f, b.y + 40.0f, 224.0f, 34.0f},
                   kSpectralAccent);
        drawSlider(pid(core::ParamId::lowWidth), "Low Width",
                   format(core::ParamId::lowWidth).c_str(), {b.x + 256.0f, b.y + 40.0f, 224.0f, 34.0f},
                   kSpectralAccent);
        drawSlider(pid(core::ParamId::highWidth), "High Width",
                   format(core::ParamId::highWidth).c_str(), {b.x + 496.0f, b.y + 40.0f, 224.0f, 34.0f},
                   kSpectralAccent);
        label(b.x + 740.0f, b.y + 40.0f, "Spectral", false, 12.0f);
        drawSegments(pid(core::ParamId::spectralWidth), core::kOffOnNames.data(), 2,
                     {b.x + 740.0f, b.y + 58.0f, 116.0f, 26.0f}, kSpectralAccent);
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "OUTPUT", kOutputAccent, "final level + meters");
        drawSlider(pid(core::ParamId::level), "Output", format(core::ParamId::level).c_str(),
                   {b.x + 16.0f, b.y + 42.0f, 250.0f, 34.0f}, kOutputAccent);
        label(b.x + 296.0f, b.y + 42.0f, "Bypass", false, 12.0f);
        drawSegments(pid(core::ParamId::bypass), core::kOffOnNames.data(), 2,
                     {b.x + 296.0f, b.y + 60.0f, 160.0f, 26.0f}, kOutputAccent);

        drawMeters({b.x + 500.0f, b.y + 42.0f, b.w - 516.0f, 74.0f});
    }

    void drawMeters(const Rect b)
    {
        const auto& t = theme();
        const float corr = std::clamp(value(pid(core::ParamId::outCorrelation)), -1.0f, 1.0f);
        const float mono = std::clamp(value(pid(core::ParamId::outMonoCompat)), 0.0f, 1.0f);

        label(b.x, b.y, "CORRELATION", true, 11.0f);
        char text[32];
        std::snprintf(text, sizeof(text), "%+.2f", corr);
        label(b.x + b.w, b.y, text, false, 11.0f, ALIGN_RIGHT | ALIGN_TOP);
        const float corrY = b.y + 20.0f;
        beginPath();
        fc(t.controlTrack);
        roundedRect(b.x, corrY, b.w, 10.0f, 3.0f);
        fill();
        const float centre = b.x + b.w * 0.5f;
        const float end = centre + corr * b.w * 0.5f;
        beginPath();
        fillColor(corr >= 0.0f ? kOutputAccent.r : kMatrixAccent.r,
                  corr >= 0.0f ? kOutputAccent.g : kMatrixAccent.g,
                  corr >= 0.0f ? kOutputAccent.b : kMatrixAccent.b, 255);
        roundedRect(std::min(centre, end), corrY, std::max(2.0f, std::fabs(end - centre)), 10.0f, 3.0f);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        moveTo(centre, corrY - 2.0f);
        lineTo(centre, corrY + 12.0f);
        stroke();

        label(b.x, b.y + 40.0f, "MONO FIT", true, 11.0f);
        std::snprintf(text, sizeof(text), "%.0f%%", mono * 100.0f);
        label(b.x + b.w, b.y + 40.0f, text, false, 11.0f, ALIGN_RIGHT | ALIGN_TOP);
        const float monoY = b.y + 60.0f;
        beginPath();
        fc(t.controlTrack);
        roundedRect(b.x, monoY, b.w, 10.0f, 3.0f);
        fill();
        if (mono > 0.002f) {
            beginPath();
            fillColor(kImageAccent.r, kImageAccent.g, kImageAccent.b, 255);
            roundedRect(b.x, monoY, b.w * mono, 10.0f, 3.0f);
            fill();
        }
    }

    [[nodiscard]] std::string format(const core::ParamId id) const
    {
        char buffer[40];
        const float v = value(pid(id));
        switch (id) {
        case core::ParamId::matrixLL:
        case core::ParamId::matrixLR:
        case core::ParamId::matrixRL:
        case core::ParamId::matrixRR:
        case core::ParamId::width:
        case core::ParamId::midGain:
        case core::ParamId::lowWidth:
        case core::ParamId::highWidth:
        case core::ParamId::level:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", v * 100.0f);
            break;
        case core::ParamId::imageDelay:
            std::snprintf(buffer, sizeof(buffer), "%.1f ms", v);
            break;
        case core::ParamId::rotation:
            std::snprintf(buffer, sizeof(buffer), "%+.0f deg", v);
            break;
        case core::ParamId::crossover:
            std::snprintf(buffer, sizeof(buffer), "%.0f Hz", v);
            break;
        case core::ParamId::invertR:
        case core::ParamId::bypass:
        case core::ParamId::spectralWidth:
            return core::kOffOnNames[core::clampInt(v, 0, 1)];
        case core::ParamId::panLaw:
            return core::kPanLawNames[core::clampInt(v, 0, 1)];
        case core::ParamId::msSolo:
            return core::kSoloNames[core::clampInt(v, 0, 2)];
        case core::ParamId::outCorrelation:
            std::snprintf(buffer, sizeof(buffer), "%+.2f", v);
            break;
        case core::ParamId::outMonoCompat:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", v * 100.0f);
            break;
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
