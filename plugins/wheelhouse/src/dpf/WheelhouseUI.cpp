#include "downspout/dpf/MagnetoKit.hpp"

#include "wheelhouse_core.hpp"
#include "wheelhouse_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::wheelhouse;

namespace {

constexpr kit::Accent kUpperAccent {198, 132, 58};    // amber
constexpr kit::Accent kLowerAccent {120, 132, 96};    // sage
constexpr kit::Accent kPedalAccent {146, 98, 78};     // walnut
constexpr kit::Accent kPercAccent {92, 140, 156};     // steel
constexpr kit::Accent kScannerAccent {150, 120, 170}; // lilac
constexpr kit::Accent kSoundAccent {186, 112, 92};    // terracotta
constexpr kit::Accent kChannelAccent {110, 128, 150};

constexpr float kStepHeight = 17.0f;                  // one drawbar position
constexpr float kRodHeight = 8.0f * kStepHeight;
constexpr float kBarPitch = 40.0f;

// The caps follow the console: the two bass footages brown, then white and black.
enum class Cap { Brown, White, Black };
constexpr std::array<Cap, core::kDrawbarCount> kManualCaps = {Cap::Brown, Cap::Brown, Cap::White, Cap::White, Cap::Black,
                                                              Cap::White, Cap::Black, Cap::Black, Cap::White};

struct Registration {
    const char* name;
    std::array<int, core::kDrawbarCount> bars;
};
constexpr std::array<Registration, 4> kRegistrations = {{
    {"Jazz",  {8, 8, 8, 0, 0, 0, 0, 0, 0}},
    {"Full",  {8, 8, 8, 8, 8, 8, 8, 8, 8}},
    {"Flute", {0, 0, 8, 4, 0, 0, 0, 0, 0}},
    {"Reed",  {0, 0, 6, 8, 6, 4, 3, 2, 0}},
}};

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::specs()[i];
            ranges[i] = {s.minimum, s.maximum, s.integer, false};
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

class WheelhouseUI : public MagnetoUI
{
public:
    WheelhouseUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) {}

protected:
    void onAction(const int id) override
    {
        // id = manual * 10 + registration (a one-shot: the drawbars are ordinary parameters, so nothing is applied in DSP).
        const int manual = id / 10, which = id % 10;
        if (manual > 1 || which >= static_cast<int>(kRegistrations.size())) return;
        const std::uint32_t first = manual == 0 ? core::kUpperDrawbar0 : core::kLowerDrawbar0;
        for (int f = 0; f < core::kDrawbarCount; ++f)
            commit(first + static_cast<std::uint32_t>(f), static_cast<float>(kRegistrations[static_cast<std::size_t>(which)].bars[static_cast<std::size_t>(f)]));
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1) {
            if (!ev.press) {
                active_ = -1;
            } else {
                const float mx = static_cast<float>(ev.pos.getX()), my = static_cast<float>(ev.pos.getY());
                for (std::size_t i = 0; i < barCount_; ++i) {
                    if (bars_[i].hit.contains(mx, my)) {
                        active_ = static_cast<int>(i);
                        setFromY(bars_[i], my);
                        return true;
                    }
                }
            }
        }
        return MagnetoUI::onMouse(ev);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (active_ >= 0 && active_ < static_cast<int>(barCount_)) {
            setFromY(bars_[static_cast<std::size_t>(active_)], static_cast<float>(ev.pos.getY()));
            return true;
        }
        return MagnetoUI::onMotion(ev);
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float mx = static_cast<float>(ev.pos.getX()), my = static_cast<float>(ev.pos.getY());
        for (std::size_t i = 0; i < barCount_; ++i) {
            if (bars_[i].hit.contains(mx, my)) {
                commit(bars_[i].parameter, value(bars_[i].parameter) + (ev.delta.getY() > 0 ? -1.0f : 1.0f));   // wheel up pushes the bar in
                return true;
            }
        }
        return false;
    }

    void onNanoDisplay() override
    {
        beginFrame();
        barCount_ = 0;
        drawHeader("WHEELHOUSE", "tonewheel organ",
                   "Pull a drawbar down to add that footage. Upper, lower and pedals answer MIDI channels 1, 2 and 3 (set below); the upper manual has percussion.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        const float gap = 12.0f;
        const float pedalW = 150.0f;
        const float manualW = (full - pedalW - 2.0f * gap) * 0.5f;
        drawManual(0, {24.0f, 108.0f, manualW, 260.0f});
        drawManual(1, {24.0f + manualW + gap, 108.0f, manualW, 260.0f});
        drawPedal({24.0f + 2.0f * (manualW + gap), 108.0f, pedalW, 260.0f});

        const float y = 380.0f, h = 200.0f;
        const float w1 = 230.0f, w2 = 300.0f, w4 = 150.0f;
        const float w3 = full - w1 - w2 - w4 - 3.0f * gap;
        drawPercussion({24.0f, y, w1, h});
        drawScanner({24.0f + w1 + gap, y, w2, h});
        drawSound({24.0f + w1 + w2 + 2.0f * gap, y, w3, h});
        drawChannels({24.0f + w1 + w2 + w3 + 3.0f * gap, y, w4, h});
    }

private:
    struct Bar {
        std::uint32_t parameter = 0;
        float top = 0.0f;                 // y of position 0
        kit::Rect hit {};
    };

    void setFromY(const Bar& b, const float y)
    {
        commit(b.parameter, std::round((y - b.top) / kStepHeight));
    }

    void drawBar(const std::uint32_t parameter, const float cx, const float top, const Cap cap, const char* footage)
    {
        const auto& t = theme();
        const float v = value(parameter);
        const float rodW = 14.0f;
        label(cx, top - 22.0f, footage, false, 11.0f, ALIGN_CENTER | ALIGN_TOP);
        // The slot, with a tick at each of the nine positions.
        beginPath();
        fc(t.controlTrack);
        roundedRect(cx - rodW * 0.5f, top - 4.0f, rodW, kRodHeight + 8.0f, 3.0f);
        fill();
        for (int s = 0; s <= 8; ++s) {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(cx - 12.0f, top + static_cast<float>(s) * kStepHeight);
            lineTo(cx - rodW * 0.5f - 1.0f, top + static_cast<float>(s) * kStepHeight);
            stroke();
        }
        const float handleY = top + v * kStepHeight;
        unsigned char r = 0, g = 0, b = 0;
        switch (cap) {
        case Cap::Brown: r = 134; g = 88; b = 56; break;
        case Cap::White: r = 226; g = 222; b = 208; break;
        case Cap::Black: r = 44; g = 44; b = 48; break;
        }
        if (v > 0.0f) {   // the rod pulled out of the console
            beginPath();
            fillColor(r, g, b, 255);
            rect(cx - 3.0f, top - 4.0f, 6.0f, handleY - top + 4.0f);
            fill();
        }
        beginPath();
        fillColor(r, g, b, 255);
        roundedRect(cx - 13.0f, handleY - 7.0f, 26.0f, 14.0f, 3.0f);
        fill();
        beginPath();
        strokeColor(0, 0, 0, 110);
        strokeWidth(1.0f);
        roundedRect(cx - 13.0f, handleY - 7.0f, 26.0f, 14.0f, 3.0f);
        stroke();
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
        label(cx, top + kRodHeight + 12.0f, buf, v > 0.0f ? false : true, 12.0f, ALIGN_CENTER | ALIGN_TOP);
        if (barCount_ < bars_.size())
            bars_[barCount_++] = {parameter, top, {cx - 18.0f, top - 12.0f, 36.0f, kRodHeight + 24.0f}};
    }

    void drawManual(const int manual, const Rect b)
    {
        const bool upper = manual == 0;
        drawPanel(b, upper ? "UPPER MANUAL" : "LOWER MANUAL", upper ? kUpperAccent : kLowerAccent,
                  upper ? "channel 1; percussion and scanner" : "channel 2");
        // Registrations along the top: one-shot buttons that set the nine drawbars.
        const float bw = (b.w - 28.0f - 3.0f * 6.0f) / 4.0f;
        for (int i = 0; i < static_cast<int>(kRegistrations.size()); ++i)
            drawButton(manual * 10 + i, kRegistrations[static_cast<std::size_t>(i)].name,
                       {b.x + 14.0f + static_cast<float>(i) * (bw + 6.0f), b.y + 38.0f, bw, 22.0f}, upper ? kUpperAccent : kLowerAccent, false);
        const std::uint32_t first = upper ? core::kUpperDrawbar0 : core::kLowerDrawbar0;
        const float x0 = b.x + (b.w - 9.0f * kBarPitch) * 0.5f + kBarPitch * 0.5f;
        for (int f = 0; f < core::kDrawbarCount; ++f)
            drawBar(first + static_cast<std::uint32_t>(f), x0 + static_cast<float>(f) * kBarPitch, b.y + 100.0f,
                    kManualCaps[static_cast<std::size_t>(f)], core::kFootageNames[static_cast<std::size_t>(f)]);
    }

    void drawPedal(const Rect b)
    {
        drawPanel(b, "PEDALS", kPedalAccent, "channel 3");
        const float cx = b.x + b.w * 0.5f;
        drawBar(core::kPedalDrawbar0, cx - 30.0f, b.y + 100.0f, Cap::Brown, "16'");
        drawBar(core::kPedalDrawbar0 + 1, cx + 30.0f, b.y + 100.0f, Cap::White, "8'");
        label(b.x + b.w * 0.5f, b.y + 44.0f, "C2 to G4", true, 11.0f, ALIGN_CENTER | ALIGN_TOP);
    }

    void drawPercussion(const Rect b)
    {
        const bool on = intValue(core::kPercOn) != 0;
        drawPanel(b, "PERCUSSION", kPercAccent, on ? "1' drawbar is silent" : "upper manual only");
        const float pad = 14.0f, lw = 78.0f, w = b.w - 2.0f * pad - lw;
        static constexpr const char* kOnOff[2] = {"Off", "On"};
        static constexpr const char* kHarm[2] = {"2nd", "3rd"};
        static constexpr const char* kDecay[2] = {"Fast", "Slow"};
        static constexpr const char* kLevel[2] = {"Soft", "Normal"};
        label(b.x + pad, b.y + 46.0f, "Percussion");
        drawSegments(core::kPercOn, kOnOff, 2, {b.x + pad + lw, b.y + 42.0f, w, 24.0f}, kPercAccent);
        setDim(!on);
        label(b.x + pad, b.y + 84.0f, "Harmonic");
        drawSegments(core::kPercHarmonic, kHarm, 2, {b.x + pad + lw, b.y + 80.0f, w, 24.0f}, kPercAccent);
        label(b.x + pad, b.y + 122.0f, "Decay");
        drawSegments(core::kPercDecay, kDecay, 2, {b.x + pad + lw, b.y + 118.0f, w, 24.0f}, kPercAccent);
        label(b.x + pad, b.y + 160.0f, "Level");
        drawSegments(core::kPercLevel, kLevel, 2, {b.x + pad + lw, b.y + 156.0f, w, 24.0f}, kPercAccent);
        setDim(false);
    }

    void drawScanner(const Rect b)
    {
        static constexpr const char* kModes[7] = {"Off", "V1", "V2", "V3", "C1", "C2", "C3"};
        const int mode = intValue(core::kVibrato);
        drawPanel(b, "VIBRATO / CHORUS", kScannerAccent, mode == 0 ? "scanner off" : (mode >= 4 ? "chorus" : "vibrato"));
        const float pad = 14.0f, lw = 60.0f;
        label(b.x + pad, b.y + 40.0f, "Mode");
        drawSegments(core::kVibrato, kModes, 7, {b.x + pad, b.y + 58.0f, b.w - 2.0f * pad, 24.0f}, kScannerAccent);
        static constexpr const char* kOnOff[2] = {"Off", "On"};
        setDim(mode == 0);
        label(b.x + pad, b.y + 102.0f, "Upper");
        drawSegments(core::kVibUpper, kOnOff, 2, {b.x + pad + lw, b.y + 98.0f, b.w - 2.0f * pad - lw, 24.0f}, kScannerAccent);
        label(b.x + pad, b.y + 134.0f, "Lower");
        drawSegments(core::kVibLower, kOnOff, 2, {b.x + pad + lw, b.y + 130.0f, b.w - 2.0f * pad - lw, 24.0f}, kScannerAccent);
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.1f Hz", static_cast<double>(value(core::kScannerRate)));
        drawSlider(core::kScannerRate, "Scanner rate", buf, {b.x + pad, b.y + 164.0f, b.w - 2.0f * pad, 28.0f}, kScannerAccent);
        setDim(false);
    }

    void drawSound(const Rect b)
    {
        drawPanel(b, "SOUND", kSoundAccent, "preamp and output");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        struct Row { std::uint32_t parameter; const char* name; };
        static constexpr Row rows[5] = {{core::kClick, "Key click"}, {core::kDrive, "Drive"}, {core::kTone, "Tone"},
                                        {core::kWidth, "Width"}, {core::kVolume, "Volume"}};
        char buf[16];
        for (int i = 0; i < 5; ++i) {
            std::snprintf(buf, sizeof(buf), "%.0f %%", static_cast<double>(value(rows[i].parameter) * 100.0f));
            drawSlider(rows[i].parameter, rows[i].name, buf, {b.x + pad, b.y + 38.0f + static_cast<float>(i) * 32.0f, w, 28.0f}, kSoundAccent);
        }
    }

    void drawChannels(const Rect b)
    {
        drawPanel(b, "MIDI", kChannelAccent, "0 = off");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        const struct { std::uint32_t parameter; const char* name; } rows[3] = {
            {core::kUpperChannel, "Upper"}, {core::kLowerChannel, "Lower"}, {core::kPedalChannel, "Pedal"}};
        for (int i = 0; i < 3; ++i) {
            const int ch = intValue(rows[i].parameter);
            char buf[24];
            if (ch == 0) std::snprintf(buf, sizeof(buf), "off");
            else         std::snprintf(buf, sizeof(buf), "channel %d", ch);
            drawStepper(rows[i].parameter, rows[i].name, buf, {b.x + pad, b.y + 40.0f + static_cast<float>(i) * 46.0f, w, 22.0f});
        }
        label(b.x + pad, b.y + b.h - 22.0f, "CC 11 = swell", true, 11.0f);
    }

    std::array<Bar, 20> bars_ {};
    std::size_t barCount_ = 0;
    int active_ = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WheelhouseUI)
};

UI* createUI()
{
    return new WheelhouseUI();
}

END_NAMESPACE_DISTRHO
