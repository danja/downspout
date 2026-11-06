#include "downspout/dpf/MagnetoKit.hpp"

#include "sieve_engine.hpp"
#include "sieve_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::sieve;

namespace {

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::kParameterSpecs[i];
            ranges[i] = {s.minimum, s.maximum, (bool)s.integer};
            defaults[i] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

constexpr kit::Accent kAccent {120, 160, 120};

}  // namespace

class SieveUI : public MagnetoUI {
public:
    SieveUI()
        : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
    }

protected:
    void onAction(int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("Sieve", "spectral sieve", "");
        Rect b {10.0f, 40.0f, (float)getWidth() - 20.0f, (float)getHeight() - 50.0f};
        drawPanel(b, "SIEVE", kAccent);
        char buf[32];
        auto cell = [&](int col, int row) {
            float colW = (b.w - 28.0f - 12.0f) * 0.5f;
            return Rect{b.x + 14.0f + (float)col * (colW + 12.0f),
                        b.y + 46.0f + (float)row * 56.0f, colW, 40.0f};
        };
        std::snprintf(buf, sizeof(buf), "%d", (int)value(core::kRoot));
        drawSlider(core::kRoot, "Root", buf, cell(0, 0), kAccent);
        drawSlider(core::kScale, "Scale", buf, cell(1, 0), kAccent);
        drawSlider(core::kSource, "Source", buf, cell(0, 1), kAccent);
        drawSlider(core::kMidiCh, "MIDI In", buf, cell(1, 1), kAccent);
        std::snprintf(buf, sizeof(buf), "%.0f", value(core::kWidth));
        drawSlider(core::kWidth, "Width", buf, cell(0, 2), kAccent);
        std::snprintf(buf, sizeof(buf), "%.0f", value(core::kFloor));
        drawSlider(core::kFloor, "Floor", buf, cell(1, 2), kAccent);
        std::snprintf(buf, sizeof(buf), "%.2f", value(core::kTilt));
        drawSlider(core::kTilt, "Tilt", buf, cell(0, 3), kAccent);
        std::snprintf(buf, sizeof(buf), "%.0f%%", value(core::kMix) * 100.0f);
        drawSlider(core::kMix, "Mix", buf, cell(1, 3), kAccent);
        std::snprintf(buf, sizeof(buf), "%.0f", value(core::kLevel));
        drawSlider(core::kLevel, "Level", buf, cell(0, 4), kAccent);
    }
};

UI* createUI() { return new SieveUI(); }

END_NAMESPACE_DISTRHO