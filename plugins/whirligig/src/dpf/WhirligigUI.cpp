#include "downspout/dpf/MagnetoKit.hpp"

#include "whirligig_core.hpp"
#include "whirligig_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::whirligig;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr kit::Accent kHornAccent {198, 132, 58};     // amber
constexpr kit::Accent kDrumAccent {146, 98, 78};      // walnut
constexpr kit::Accent kSpeedAccent {92, 140, 156};    // steel
constexpr kit::Accent kSyncAccent {150, 120, 170};    // lilac
constexpr kit::Accent kOutAccent {120, 132, 96};      // sage

constexpr std::array<const char*, core::kDivisionCount> kDivisionNames = {
    "8 beats", "4 beats", "3 beats", "2 beats", "1.5 beats", "1 beat", "3/4 beat", "1/2 beat", "1/3 beat", "1/4 beat", "1/6 beat", "1/8 beat"};

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

class WhirligigUI : public MagnetoUI
{
public:
    WhirligigUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        // The standalone used for screenshots has no audio or transport: show the rotors part-way round at the Slow speeds.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            setValue(core::kStatusHornHz, 0.8f);
            setValue(core::kStatusDrumHz, 0.67f);
            setValue(core::kStatusHornTurn, 0.10f);
            setValue(core::kStatusDrumTurn, 0.58f);
        }
    }

protected:
    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_ && core::isOutput(index)) return;   // keep the demo readings
        MagnetoUI::parameterChanged(index, v);
    }

    void onAction(const int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("WHIRLIGIG", "rotary speaker",
                   "A crossover splits the sound between a horn and a drum rotor; two microphones hear each turn as a swell and a pitch wobble. Run free in Hz or lock to the host's beat.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawCabinet({24.0f, 108.0f, full, 184.0f});
        const float gap = 12.0f, y = 304.0f, h = 240.0f;
        const float wSpeed = 420.0f, wRotor = (full - wSpeed - 2.0f * gap) * 0.5f;
        drawSpeed({24.0f, y, wSpeed, h});
        drawRotor(true, {24.0f + wSpeed + gap, y, wRotor, h});
        drawRotor(false, {24.0f + wSpeed + wRotor + 2.0f * gap, y, wRotor, h});
        drawOutput({24.0f, y + h + 12.0f, full, 88.0f});
    }

private:
    [[nodiscard]] static std::string rpm(const float hz)
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.2f Hz  (%.0f rpm)", static_cast<double>(hz), static_cast<double>(hz) * 60.0);
        return buf;
    }

    // Screen angle of a rotor a given fraction of a turn round, clockwise from the top.
    [[nodiscard]] static float screenAngle(const float turn) { return -0.5f * kPi + 2.0f * kPi * turn; }

    void polygon(const std::array<std::pair<float, float>, 4>& pts, const kit::Accent c, const unsigned char alpha = 255)
    {
        beginPath();
        moveTo(pts[0].first, pts[0].second);
        for (std::size_t i = 1; i < pts.size(); ++i) lineTo(pts[i].first, pts[i].second);
        closePath();
        fillColor(c.r, c.g, c.b, alpha);
        fill();
    }

    void drawMics(const float cx, const float cy, const float radius)
    {
        const auto& t = theme();
        const float half = value(core::kMicAngle) / 720.0f;
        const float turns[2] = {half, -half};
        const char* names[2] = {"L", "R"};
        for (int i = 0; i < 2; ++i) {
            const float a = screenAngle(turns[i]);
            const float mx = cx + std::cos(a) * radius, my = cy + std::sin(a) * radius;
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(cx + std::cos(a) * (radius - 22.0f), cy + std::sin(a) * (radius - 22.0f));
            lineTo(mx, my);
            stroke();
            beginPath();
            fc(t.textPrimary);
            circle(mx, my, 8.0f);
            fill();
            label(mx, my, names[i], false, 10.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    void drawHorn(const float cx, const float cy, const float turn)
    {
        const float a = screenAngle(turn);
        const float dx = std::cos(a), dy = std::sin(a), px = -dy, py = dx;
        // The bell flares from the hub; the dummy horn opposite balances it.
        polygon({{{cx + dx * 14.0f + px * 5.0f, cy + dy * 14.0f + py * 5.0f}, {cx + dx * 52.0f + px * 17.0f, cy + dy * 52.0f + py * 17.0f},
                  {cx + dx * 52.0f - px * 17.0f, cy + dy * 52.0f - py * 17.0f}, {cx + dx * 14.0f - px * 5.0f, cy + dy * 14.0f - py * 5.0f}}},
                kHornAccent);
        polygon({{{cx - dx * 10.0f + px * 8.0f, cy - dy * 10.0f + py * 8.0f}, {cx - dx * 34.0f + px * 8.0f, cy - dy * 34.0f + py * 8.0f},
                  {cx - dx * 34.0f - px * 8.0f, cy - dy * 34.0f - py * 8.0f}, {cx - dx * 10.0f - px * 8.0f, cy - dy * 10.0f - py * 8.0f}}},
                kHornAccent, 110);
        beginPath();
        fc(theme().textPrimary);
        circle(cx, cy, 6.0f);
        fill();
    }

    void drawDrum(const float cx, const float cy, const float turn)
    {
        const auto& t = theme();
        beginPath();
        sc(t.border);
        strokeWidth(2.0f);
        circle(cx, cy, 52.0f);
        stroke();
        // The open slot of the drum faces `a`: a wedge of the ring is filled in.
        const float a = screenAngle(turn);
        beginPath();
        moveTo(cx, cy);
        constexpr int kSteps = 16;
        for (int i = 0; i <= kSteps; ++i) {
            const float b = a - 0.55f + 1.1f * static_cast<float>(i) / kSteps;
            lineTo(cx + std::cos(b) * 50.0f, cy + std::sin(b) * 50.0f);
        }
        closePath();
        fillColor(kDrumAccent.r, kDrumAccent.g, kDrumAccent.b, 255);
        fill();
        beginPath();
        fc(t.textPrimary);
        circle(cx, cy, 5.0f);
        fill();
    }

    void drawCabinet(const Rect b)
    {
        const bool locked = value(core::kStatusLocked) > 0.5f;
        const bool synced = intValue(core::kSync) != 0;
        drawPanel(b, "CABINET", kSpeedAccent, synced ? (locked ? "locked to the transport" : "beat mode: waiting for the transport") : "free running");
        const float cy = b.y + 98.0f;
        const float cxHorn = b.x + b.w * 0.22f, cxDrum = b.x + b.w * 0.78f;
        drawMics(cxHorn, cy, 74.0f);
        drawMics(cxDrum, cy, 74.0f);
        drawHorn(cxHorn, cy, value(core::kStatusHornTurn));
        drawDrum(cxDrum, cy, value(core::kStatusDrumTurn));
        label(cxHorn + 92.0f, cy - 26.0f, "HORN", false, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(cxHorn + 92.0f, cy - 6.0f, rpm(value(core::kStatusHornHz)).c_str(), true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(cxHorn + 92.0f, cy + 12.0f, "highs", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(cxDrum - 92.0f, cy - 26.0f, "DRUM", false, 13.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        label(cxDrum - 92.0f, cy - 6.0f, rpm(value(core::kStatusDrumHz)).c_str(), true, 11.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        label(cxDrum - 92.0f, cy + 12.0f, "lows", true, 11.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        char split[40];
        std::snprintf(split, sizeof(split), "split at %.0f Hz", static_cast<double>(value(core::kCrossover)));
        label(b.x + b.w * 0.5f, cy + 6.0f, split, true, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(b.x + b.w * 0.5f, cy + 24.0f, "microphones L and R", true, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawSpeed(const Rect b)
    {
        const bool synced = intValue(core::kSync) != 0;
        drawPanel(b, "SPEED", kSpeedAccent, synced ? "turns per beat, locked to the bar" : "rotor speeds in Hz");
        const float pad = 14.0f, w = b.w - 2.0f * pad, lw = 56.0f;
        static constexpr const char* kSpeeds[3] = {"Stop", "Slow", "Fast"};
        static constexpr const char* kSync[2] = {"Free", "Beat"};
        label(b.x + pad, b.y + 46.0f, "Speed");
        drawSegments(core::kSpeed, kSpeeds, 3, {b.x + pad + lw, b.y + 42.0f, w - lw, 24.0f}, kSpeedAccent);
        label(b.x + pad, b.y + 82.0f, "Sync");
        drawSegments(core::kSync, kSync, 2, {b.x + pad + lw, b.y + 78.0f, w - lw, 24.0f}, kSyncAccent);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f deg", static_cast<double>(value(core::kSyncPhase)));
        setDim(!synced);
        drawSlider(core::kSyncPhase, "Phase (where each bar starts)", buf, {b.x + pad, b.y + 112.0f, w, 28.0f}, kSyncAccent);
        setDim(false);

        const float colW = (w - 16.0f) * 0.5f;
        const struct { std::uint32_t freeParam, divParam; const char* name; float x, y; kit::Accent accent; } rows[4] = {
            {core::kHornSlow, core::kHornSlowDiv, "Horn slow", 0.0f, 152.0f, kHornAccent},
            {core::kHornFast, core::kHornFastDiv, "Horn fast", 0.0f, 192.0f, kHornAccent},
            {core::kDrumSlow, core::kDrumSlowDiv, "Drum slow", colW + 16.0f, 152.0f, kDrumAccent},
            {core::kDrumFast, core::kDrumFastDiv, "Drum fast", colW + 16.0f, 192.0f, kDrumAccent}};
        for (const auto& r : rows) {
            const Rect box {b.x + pad + r.x, b.y + r.y, colW, 28.0f};
            if (synced) {
                const int idx = std::clamp(intValue(r.divParam), 0, core::kDivisionCount - 1);
                drawSlider(r.divParam, r.name, kDivisionNames[static_cast<std::size_t>(idx)], box, r.accent);
            } else {
                std::snprintf(buf, sizeof(buf), "%.2f Hz", static_cast<double>(value(r.freeParam)));
                drawSlider(r.freeParam, r.name, buf, box, r.accent);
            }
        }
    }

    void drawRotor(const bool horn, const Rect b)
    {
        drawPanel(b, horn ? "HORN" : "DRUM", horn ? kHornAccent : kDrumAccent, horn ? "above the crossover" : "below the crossover");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        const auto depth = horn ? core::kHornDepth : core::kDrumDepth;
        const auto doppler = horn ? core::kHornDoppler : core::kDrumDoppler;
        const auto level = horn ? core::kHornLevel : core::kDrumLevel;
        const auto accel = horn ? core::kHornAccel : core::kDrumAccel;
        const kit::Accent accent = horn ? kHornAccent : kDrumAccent;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f %%", static_cast<double>(value(depth) * 100.0f));
        drawSlider(depth, "Swell (amplitude)", buf, {b.x + pad, b.y + 40.0f, w, 28.0f}, accent);
        std::snprintf(buf, sizeof(buf), "%.0f %%", static_cast<double>(value(doppler) * 100.0f));
        drawSlider(doppler, "Doppler (pitch)", buf, {b.x + pad, b.y + 82.0f, w, 28.0f}, accent);
        std::snprintf(buf, sizeof(buf), "%+.1f dB", static_cast<double>(value(level)));
        drawSlider(level, "Level", buf, {b.x + pad, b.y + 124.0f, w, 28.0f}, accent);
        std::snprintf(buf, sizeof(buf), "%.1f s", static_cast<double>(value(accel)));
        drawSlider(accel, "Inertia (speed change)", buf, {b.x + pad, b.y + 166.0f, w, 28.0f}, accent);
    }

    void drawOutput(const Rect b)
    {
        drawPanel(b, "OUTPUT", kOutAccent, "crossover, microphones, mix");
        const float pad = 14.0f, gap = 14.0f;
        const float colW = (b.w - 2.0f * pad - 4.0f * gap) / 5.0f;
        float x = b.x + pad;
        const float y = b.y + 38.0f;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(value(core::kCrossover)));
        drawSlider(core::kCrossover, "Crossover", buf, {x, y, colW, 28.0f}, kOutAccent);
        x += colW + gap;
        std::snprintf(buf, sizeof(buf), "%.0f deg", static_cast<double>(value(core::kMicAngle)));
        drawSlider(core::kMicAngle, "Mic angle", buf, {x, y, colW, 28.0f}, kOutAccent);
        x += colW + gap;
        std::snprintf(buf, sizeof(buf), "%.0f %%", static_cast<double>(value(core::kMix) * 100.0f));
        drawSlider(core::kMix, "Mix", buf, {x, y, colW, 28.0f}, kOutAccent);
        x += colW + gap;
        std::snprintf(buf, sizeof(buf), "%+.1f dB", static_cast<double>(value(core::kOutput)));
        drawSlider(core::kOutput, "Output", buf, {x, y, colW, 28.0f}, kOutAccent);
        x += colW + gap;
        const int ch = intValue(core::kCcChannel);
        if (ch == 0) std::snprintf(buf, sizeof(buf), "CC off");
        else         std::snprintf(buf, sizeof(buf), "ch %d: CC1 CC2", ch);
        drawStepper(core::kCcChannel, "CC channel", buf, {x, y - 2.0f, colW, 22.0f});
    }

    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WhirligigUI)
};

UI* createUI()
{
    return new WhirligigUI();
}

END_NAMESPACE_DISTRHO
