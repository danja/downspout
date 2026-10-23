#include "downspout/dpf/MagnetoKit.hpp"

#include "monochord_core.hpp"
#include "monochord_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace core = downspout::monochord;

namespace {

constexpr kit::Accent kStringAccent {198, 132, 58};   // amber
constexpr kit::Accent kExciterAccent {198, 120, 64};  // copper
constexpr kit::Accent kLoopAccent {92, 140, 156};     // cold steel
constexpr kit::Accent kBodyAccent {120, 132, 96};     // sage
constexpr std::array<const char*, 3> kExciterNames = {"Pluck", "Bow", "Hammer"};
constexpr double kPi = 3.14159265358979323846;
constexpr int kHarmonics = 20;

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

class MonochordUI : public MagnetoUI
{
public:
    MonochordUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        // The standalone used for screenshots starts at the defaults; show a more characteristic patch.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            const std::pair<std::uint32_t, float> values[] = {
                {core::kExciter, 0}, {core::kPosition, 0.18f}, {core::kHardness, 0.75f}, {core::kDecay, 4.0f},
                {core::kDamping, 0.35f}, {core::kStiffness, 0.6f}, {core::kBodyLevel, 0.4f}, {core::kPickup, 0.28f},
            };
            for (const auto& [id, v] : values) setValue(id, v);
        }
    }

protected:
    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_)
            return;  // keep the demo patch
        MagnetoUI::parameterChanged(index, v);
    }

    void onAction(const int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("MONOCHORD", "waveguide string",
                   "Pluck, bow or strike a string. The plot shows how it starts and which harmonics that gives; "
                   "Damping, Stiffness and Pickup shape what follows.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawStringPanel({24.0f, 108.0f, full, 214.0f});
        const float gap = 12.0f;
        const float third = (full - 2.0f * gap) / 3.0f;
        drawExciterPanel({24.0f, 334.0f, third, 280.0f});
        drawLoopPanel({24.0f + third + gap, 334.0f, third, 280.0f});
        drawBodyPanel({24.0f + 2.0f * (third + gap), 334.0f, third, 280.0f});
    }

private:
    [[nodiscard]] core::Parameters currentParameters() const
    {
        core::Parameters p;
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) core::field(p, i) = value(i);
        return p;
    }

    void slider(const std::uint32_t id, const char* name, const Rect r, const Accent a)
    {
        drawSlider(id, name, format(id).c_str(), r, a);
    }

    void drawStringPanel(const Rect b)
    {
        drawPanel(b, "THE STRING", kStringAccent, "how it starts, and the harmonics that gives");
        const auto& t = theme();
        const core::Parameters p = currentParameters();
        const auto exciter = static_cast<core::Exciter>(static_cast<int>(p.exciter));
        const float pad = 14.0f;
        const Rect shape {b.x + pad, b.y + 46.0f, b.w * 0.5f - pad - 8.0f, b.h - 46.0f - 28.0f};
        const Rect bars {b.x + b.w * 0.5f + 8.0f, b.y + 46.0f, b.w * 0.5f - pad - 8.0f, b.h - 46.0f - 28.0f};

        // Left: the string's initial displacement between the nut and the bridge.
        drawPlotFrame(shape);
        const float midY = shape.y + shape.h * 0.5f;
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        moveTo(shape.x, midY);
        lineTo(shape.x + shape.w, midY);
        stroke();
        const int n = 256;
        std::vector<float> s(static_cast<size_t>(n));
        core::excitation(exciter, n, p, s.data());
        if (exciter != core::Exciter::Bow) {
            beginPath();
            strokeColor(kStringAccent.r, kStringAccent.g, kStringAccent.b, 255);
            strokeWidth(2.4f);
            for (int i = 0; i < n; ++i) {
                const float x = shape.x + static_cast<float>(i) / static_cast<float>(n - 1) * shape.w;
                const float y = midY - s[static_cast<size_t>(i)] * shape.h * 0.4f;
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            stroke();
        } else {
            // A bow: a contact point, drawn as a short vertical bar.
            const float bx = shape.x + p.position * shape.w;
            beginPath();
            strokeColor(kStringAccent.r, kStringAccent.g, kStringAccent.b, 255);
            strokeWidth(4.0f);
            moveTo(bx, midY - shape.h * 0.32f);
            lineTo(bx, midY + shape.h * 0.32f);
            stroke();
        }
        const float px = shape.x + p.position * shape.w;
        beginPath();
        fillColor(kExciterAccent.r, kExciterAccent.g, kExciterAccent.b, 255);
        moveTo(px, shape.y + shape.h - 2.0f);
        lineTo(px - 6.0f, shape.y + shape.h + 8.0f);
        lineTo(px + 6.0f, shape.y + shape.h + 8.0f);
        closePath();
        fill();
        label(px, shape.y + shape.h + 9.0f, exciter == core::Exciter::Bow ? "bow" : (exciter == core::Exciter::Pluck ? "pluck" : "strike"), true, 10.0f,
              ALIGN_CENTER | ALIGN_TOP);
        if (p.pickup > 0.0f) {
            const float qx = shape.x + p.pickup * shape.w;
            beginPath();
            strokeColor(kLoopAccent.r, kLoopAccent.g, kLoopAccent.b, 255);
            strokeWidth(1.6f);
            moveTo(qx, shape.y + 4.0f);
            lineTo(qx, shape.y + shape.h - 4.0f);
            stroke();
            label(qx + 4.0f, shape.y + 6.0f, "pickup", true, 10.0f);
        }
        label(shape.x + 4.0f, shape.y + shape.h + 9.0f, "nut", true, 10.0f);
        label(shape.x + shape.w - 4.0f, shape.y + shape.h + 9.0f, "bridge", true, 10.0f, ALIGN_RIGHT | ALIGN_TOP);

        // Right: the first harmonics, in dB below the strongest, from the same shape and the pickup comb.
        drawPlotFrame(bars);
        std::array<double, kHarmonics> mag {};
        double top = 1e-9;
        for (int k = 1; k <= kHarmonics; ++k) {
            double re = 0.0, im = 0.0;
            if (exciter == core::Exciter::Bow) {
                re = 1.0 / k;   // a bowed string is rich: about 1/k
            } else {
                for (int i = 0; i < n; ++i) {
                    re += s[static_cast<size_t>(i)] * std::cos(2.0 * kPi * k * i / n);
                    im += s[static_cast<size_t>(i)] * std::sin(2.0 * kPi * k * i / n);
                }
            }
            double m = std::hypot(re, im);
            if (p.pickup > 0.0f) m *= std::fabs(std::sin(kPi * k * p.pickup));
            mag[static_cast<size_t>(k - 1)] = m;
            top = std::max(top, m);
        }
        const float barW = bars.w / static_cast<float>(kHarmonics);
        for (int k = 0; k < kHarmonics; ++k) {
            const double db = 20.0 * std::log10(mag[static_cast<size_t>(k)] / top + 1e-6);
            const float h = static_cast<float>(std::clamp((db + 60.0) / 60.0, 0.0, 1.0)) * (bars.h - 10.0f);
            beginPath();
            fillColor(kStringAccent.r, kStringAccent.g, kStringAccent.b, k == 0 ? 255 : 210);
            rect(bars.x + static_cast<float>(k) * barW + 2.0f, bars.y + bars.h - 4.0f - h, barW - 4.0f, h);
            fill();
            if (k == 0 || (k + 1) % 5 == 0) {
                char num[8];
                std::snprintf(num, sizeof(num), "%d", k + 1);
                label(bars.x + (static_cast<float>(k) + 0.5f) * barW, bars.y + bars.h + 4.0f, num, true, 10.0f, ALIGN_CENTER | ALIGN_TOP);
            }
        }
        label(bars.x + bars.w - 4.0f, bars.y + 4.0f, "harmonics, 0 to -60 dB", true, 10.0f, ALIGN_RIGHT | ALIGN_TOP);
    }

    void drawExciterPanel(const Rect b)
    {
        drawPanel(b, "EXCITER", kExciterAccent);
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        float y = b.y + 62.0f;
        label(b.x + pad, y - 16.0f, "Excite with");
        drawSegments(core::kExciter, kExciterNames.data(), 3, {b.x + pad, y, w, 24.0f}, kExciterAccent);
        y += 40.0f;
        const bool bow = intValue(core::kExciter) == 1;
        slider(core::kPosition, bow ? "Bow position" : "Position (pluck or strike)", {b.x + pad, y, w, 34.0f}, kExciterAccent);
        y += 52.0f;
        setDim(bow);
        slider(core::kHardness, "Hardness", {b.x + pad, y, w, 34.0f}, kExciterAccent);
        setDim(!bow);
        y += 52.0f;
        slider(core::kBowPressure, "Bow pressure", {b.x + pad, y, w, 34.0f}, kExciterAccent);
        setDim(false);
    }

    void drawLoopPanel(const Rect b)
    {
        drawPanel(b, "STRING", kLoopAccent);
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        float y = b.y + 50.0f;
        slider(core::kDecay, "Decay (fundamental, 60 dB)", {b.x + pad, y, w, 34.0f}, kLoopAccent);
        y += 56.0f;
        slider(core::kDamping, "Damping (high partials)", {b.x + pad, y, w, 34.0f}, kLoopAccent);
        y += 56.0f;
        slider(core::kStiffness, "Stiffness (sharpens partials)", {b.x + pad, y, w, 34.0f}, kLoopAccent);
        y += 56.0f;
        slider(core::kRelease, "Release (damper)", {b.x + pad, y, w, 34.0f}, kLoopAccent);
    }

    void drawBodyPanel(const Rect b)
    {
        drawPanel(b, "BODY, PICKUP AND OUTPUT", kBodyAccent);
        const float pad = 14.0f, gap = 14.0f;
        const float w = b.w - 2.0f * pad;
        const float half = (w - gap) * 0.5f;
        float y = b.y + 50.0f;
        slider(core::kBodySize, "Body size", {b.x + pad, y, half, 34.0f}, kBodyAccent);
        slider(core::kBodyLevel, "Body level", {b.x + pad + half + gap, y, half, 34.0f}, kBodyAccent);
        y += 56.0f;
        slider(core::kPickup, "Pickup position (0 = off)", {b.x + pad, y, w, 34.0f}, kBodyAccent);
        y += 56.0f;
        slider(core::kMaster, "Master", {b.x + pad, y, half, 34.0f}, kBodyAccent);
        slider(core::kVelocity, "Velocity", {b.x + pad + half + gap, y, half, 34.0f}, kBodyAccent);
        y += 56.0f;
        slider(core::kBendRange, "Bend range", {b.x + pad, y, half, 34.0f}, kBodyAccent);
    }

    [[nodiscard]] std::string format(const std::uint32_t id) const
    {
        char buffer[32];
        const float v = value(id);
        switch (id) {
        case core::kDecay:     std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(v)); break;
        case core::kRelease:   std::snprintf(buffer, sizeof(buffer), v >= 1000.0f ? "%.2f s" : "%.0f ms", v >= 1000.0f ? static_cast<double>(v) / 1000.0 : static_cast<double>(v)); break;
        case core::kBendRange: std::snprintf(buffer, sizeof(buffer), "%.0f st", static_cast<double>(v)); break;
        case core::kPickup:
            if (v <= 0.001f) return "off";
            std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v));
            break;
        default:               std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v)); break;
        }
        return buffer;
    }

    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonochordUI)
};

UI* createUI()
{
    return new MonochordUI();
}

END_NAMESPACE_DISTRHO
