#include "downspout/dpf/MagnetoKit.hpp"

#include "mouth_engine.hpp"
#include "mouth_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::mouth;

namespace {

constexpr kit::Accent kPadAccent {198, 132, 58};      // amber
constexpr kit::Accent kPlotAccent {92, 140, 156};     // cold steel
constexpr kit::Accent kControlAccent {128, 152, 104}; // sage
constexpr kit::Accent kLeftColour {208, 118, 58};
constexpr kit::Accent kRightColour {70, 132, 168};

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::kParameterSpecs[i];
            ranges[i] = {s.minimum, s.maximum, s.integer};
            defaults[i] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

std::string axisHz(const float hz)
{
    char buffer[16];
    if (hz >= 1000.0f)
        std::snprintf(buffer, sizeof(buffer), "%.0fk", static_cast<double>(hz) / 1000.0);
    else
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(hz)));
    return buffer;
}

}  // namespace

class MouthUI : public MagnetoUI {
public:
    MouthUI()
        : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
    }

protected:
    // Segments change the voice; the pad is handled in the mouse overrides below.
    void onAction(int) override {}

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1) {
            if (!ev.press) {
                dragging_ = false;
            } else if (pad_.contains(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()))) {
                dragging_ = true;
                movePad(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
                return true;
            }
        }
        return MagnetoUI::onMouse(ev);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragging_) {
            movePad(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
            return true;
        }
        return MagnetoUI::onMotion(ev);
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("Mouth", "vowel filter",
                   "puts a vowel on any sound: drag the pad to morph between A, E, I, O and U, then shape the voice.");
        drawPadPanel({24.0f, 108.0f, 400.0f, 508.0f});
        drawResponsePanel({440.0f, 108.0f, 456.0f, 214.0f});
        drawControlsPanel({440.0f, 334.0f, 456.0f, 282.0f});
    }

private:
    void movePad(const float mx, const float my)
    {
        commit(core::kVowelX, std::clamp((mx - pad_.x) / pad_.w, 0.0f, 1.0f) * 100.0f);
        commit(core::kVowelY, std::clamp((my - pad_.y) / pad_.h, 0.0f, 1.0f) * 100.0f);
    }

    [[nodiscard]] core::Parameters current() const
    {
        core::Parameters p;
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            core::field(p, i) = value(i);
        return p;
    }

    void drawPadPanel(const Rect b)
    {
        drawPanel(b, "VOWEL", kPadAccent, "drag to morph");
        const auto& t = theme();
        pad_ = {b.x + 36.0f, b.y + 56.0f, b.w - 36.0f - 20.0f, b.h - 56.0f - 66.0f};
        drawPlotFrame(pad_);

        // Axes, as on a phonetics chart: vertical = how open the mouth is, horizontal = tongue front to back.
        label(pad_.x + pad_.w * 0.5f, pad_.y + pad_.h + 8.0f, "tongue position: front to back", true, 11.0f,
              ALIGN_CENTER | ALIGN_TOP);
        {
            // The five formant frequencies the pad position produces, as heard on the left.
            const core::Parameters p = current();
            const core::FormantSet set = core::shapeFormants(core::blendVowel(p.vowelX, p.vowelY, static_cast<int>(p.voice)),
                                                             p.shiftSt, p.resonance, 0.0f, 0, 48000.0f);
            char line[96];
            std::snprintf(line, sizeof(line), "formants  %d   %d   %d   %d   %d Hz", static_cast<int>(std::lround(set[0].freq)),
                          static_cast<int>(std::lround(set[1].freq)), static_cast<int>(std::lround(set[2].freq)),
                          static_cast<int>(std::lround(set[3].freq)), static_cast<int>(std::lround(set[4].freq)));
            label(pad_.x + pad_.w * 0.5f, pad_.y + pad_.h + 34.0f, line, false, 12.0f, ALIGN_CENTER | ALIGN_TOP);
        }
        label(b.x + 12.0f, pad_.y + 6.0f, "closed", true, 11.0f);
        label(b.x + 12.0f, pad_.y + pad_.h - 18.0f, "open", true, 11.0f);

        // Faint guides through the cursor.
        const float cx = pad_.x + pad_.w * value(core::kVowelX) * 0.01f;
        const float cy = pad_.y + pad_.h * value(core::kVowelY) * 0.01f;
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        moveTo(cx, pad_.y);
        lineTo(cx, pad_.y + pad_.h);
        moveTo(pad_.x, cy);
        lineTo(pad_.x + pad_.w, cy);
        stroke();

        // The five vowels.
        for (std::size_t v = 0; v < core::kVowels; ++v) {
            const float vx = pad_.x + pad_.w * core::kPadPoints[v].x * 0.01f;
            const float vy = pad_.y + pad_.h * core::kPadPoints[v].y * 0.01f;
            beginPath();
            fc(t.buttonFace);
            circle(vx, vy, 19.0f);
            fill();
            beginPath();
            sc(t.border);
            strokeWidth(1.5f);
            circle(vx, vy, 19.0f);
            stroke();
            label(vx, vy, core::kVowelNames[v], false, 17.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        }

        // The cursor.
        beginPath();
        fillColor(kPadAccent.r, kPadAccent.g, kPadAccent.b, 70);
        circle(cx, cy, 15.0f);
        fill();
        beginPath();
        strokeColor(kPadAccent.r, kPadAccent.g, kPadAccent.b, 255);
        strokeWidth(3.0f);
        circle(cx, cy, 15.0f);
        stroke();
    }

    void drawResponsePanel(const Rect b)
    {
        drawPanel(b, "FILTER RESPONSE", kPlotAccent, "what the voice does to each frequency");
        const auto& t = theme();
        const Rect plot {b.x + 40.0f, b.y + 44.0f, b.w - 40.0f - 14.0f, b.h - 44.0f - 28.0f};
        drawPlotFrame(plot);

        const double lowHz = 80.0, highHz = 10000.0;
        const float topDb = 30.0f, bottomDb = -30.0f;
        const auto xOf = [&](const double hz) {
            return plot.x + static_cast<float>(std::log(hz / lowHz) / std::log(highHz / lowHz)) * plot.w;
        };
        const auto yOf = [&](const float db) {
            return plot.y + (topDb - std::clamp(db, bottomDb, topDb)) / (topDb - bottomDb) * plot.h;
        };

        for (const float db : {30.0f, 15.0f, 0.0f, -15.0f, -30.0f}) {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(plot.x, yOf(db));
            lineTo(plot.x + plot.w, yOf(db));
            stroke();
            char buf[12];
            std::snprintf(buf, sizeof(buf), "%+d", static_cast<int>(db));
            label(plot.x - 6.0f, yOf(db), buf, true, 10.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        for (const float hz : {100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f}) {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(xOf(hz), plot.y);
            lineTo(xOf(hz), plot.y + plot.h);
            stroke();
            label(xOf(hz), plot.y + plot.h + 5.0f, axisHz(hz).c_str(), true, 10.0f, ALIGN_CENTER | ALIGN_TOP);
        }
        label(plot.x + plot.w, plot.y + plot.h + 17.0f, "Hz", true, 10.0f, ALIGN_RIGHT | ALIGN_TOP);
        label(plot.x - 6.0f, plot.y + plot.h + 5.0f, "dB", true, 10.0f, ALIGN_RIGHT | ALIGN_TOP);

        const core::Parameters p = current();
        const bool stereo = p.spread > 0.5f;
        for (const int side : {1, 0}) {
            if (side == 1 && !stereo)
                continue;
            const kit::Accent colour = side == 1 ? kRightColour : (stereo ? kLeftColour : kPlotAccent);
            beginPath();
            strokeColor(colour.r, colour.g, colour.b, 255);
            strokeWidth(2.5f);
            constexpr int kPoints = 180;
            for (int i = 0; i < kPoints; ++i) {
                const double hz = lowHz * std::pow(highHz / lowHz, static_cast<double>(i) / (kPoints - 1));
                const float x = plot.x + plot.w * static_cast<float>(i) / (kPoints - 1);
                const float y = yOf(core::responseDb(p, 48000.0, static_cast<float>(hz), side));
                if (i == 0)
                    moveTo(x, y);
                else
                    lineTo(x, y);
            }
            stroke();
        }
        if (stereo) {
            float lx = plot.x + 12.0f;
            for (const bool right : {false, true}) {
                const kit::Accent colour = right ? kRightColour : kLeftColour;
                beginPath();
                fillColor(colour.r, colour.g, colour.b, 255);
                roundedRect(lx, plot.y + 10.0f, 16.0f, 5.0f, 2.5f);
                fill();
                label(lx + 22.0f, plot.y + 6.0f, right ? "Right" : "Left", false, 11.0f);
                lx += 60.0f;
            }
        }
    }

    void drawControlsPanel(const Rect b)
    {
        drawPanel(b, "VOICE", kControlAccent, "Drift CC 1-4: vowel X, vowel Y, shift, resonance");
        const float x = b.x + 14.0f;
        const float colW = (b.w - 28.0f - 22.0f) * 0.5f;
        const float x2 = x + colW + 22.0f;

        static const char* const kNames[] = {"Bass", "Tenor", "Alto", "Soprano"};
        label(x, b.y + 42.0f, "Voice");
        drawSegments(core::kVoice, kNames, 4, {x, b.y + 58.0f, b.w - 28.0f, 28.0f}, kControlAccent);

        char buf[32];
        float y = b.y + 98.0f;
        const float pitch = 44.0f;
        std::snprintf(buf, sizeof(buf), "%+.1f st", static_cast<double>(value(core::kShift)));
        drawSlider(core::kShift, "Shift", buf, {x, y, colW, 34.0f}, kControlAccent);
        std::snprintf(buf, sizeof(buf), "%d %%", intValue(core::kResonance));
        drawSlider(core::kResonance, "Resonance", buf, {x2, y, colW, 34.0f}, kControlAccent);
        y += pitch;
        std::snprintf(buf, sizeof(buf), "%d %%", intValue(core::kSpread));
        drawSlider(core::kSpread, "Spread", buf, {x, y, colW, 34.0f}, kControlAccent);
        std::snprintf(buf, sizeof(buf), "%d %%", intValue(core::kTilt));
        drawSlider(core::kTilt, "Air", buf, {x2, y, colW, 34.0f}, kControlAccent);
        y += pitch;
        std::snprintf(buf, sizeof(buf), "%d dB", intValue(core::kDrive));
        drawSlider(core::kDrive, "Drive", buf, {x, y, colW, 34.0f}, kControlAccent);
        std::snprintf(buf, sizeof(buf), "%d %%", intValue(core::kMix));
        drawSlider(core::kMix, "Mix", buf, {x2, y, colW, 34.0f}, kControlAccent);
        y += pitch;
        std::snprintf(buf, sizeof(buf), "%+d dB", intValue(core::kOutput));
        drawSlider(core::kOutput, "Output", buf, {x, y, colW, 34.0f}, kControlAccent);
        const int cc = intValue(core::kCcChannel);
        if (cc == 0)
            std::snprintf(buf, sizeof(buf), "off");
        else
            std::snprintf(buf, sizeof(buf), "channel %d", cc);
        drawSlider(core::kCcChannel, "CC Channel", buf, {x2, y, colW, 34.0f}, kControlAccent);
    }

    Rect pad_ {};
    bool dragging_ = false;
};

UI* createUI() { return new MouthUI(); }

END_NAMESPACE_DISTRHO
