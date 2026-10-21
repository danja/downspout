#include "downspout/dpf/MagnetoKit.hpp"

#include "grist_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::grist;

namespace {

constexpr kit::Accent kGrainAccent {176, 112, 62};    // copper
constexpr kit::Accent kTextureAccent {92, 140, 156};  // cold steel
constexpr kit::Accent kCloudAccent {94, 158, 112};    // leaf green
constexpr kit::Accent kLoopAccent {120, 126, 170};    // slate violet

constexpr const char* kTimingNames[2] = {"Free (ms)", "Tempo sync"};
constexpr const char* kOnOffNames[2] = {"Off", "On"};
constexpr const char* kFilterModes[3] = {"Lowpass", "Highpass", "Bandpass"};
constexpr const char* kDirections[3] = {"Up", "Down", "Up & down"};

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

std::string milliseconds(const double ms)
{
    char buffer[32];
    if (ms < 10.0) std::snprintf(buffer, sizeof(buffer), "%.1f ms", ms);
    else if (ms < 1000.0) std::snprintf(buffer, sizeof(buffer), "%.0f ms", ms);
    else std::snprintf(buffer, sizeof(buffer), "%.2f s", ms / 1000.0);
    return buffer;
}

}  // namespace

class GristUI : public MagnetoUI {
public:
    GristUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) {}

protected:
    void onAction(int) override {}

    // Window and Feedback delay run from 1 ms to 1 s, so their sliders are logarithmic; the kit's
    // own sliders are linear. They are drawn and dragged here.
    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1) {
            if (!ev.press) {
                activeLog_ = -1;
            } else {
                const float mx = static_cast<float>(ev.pos.getX());
                const float my = static_cast<float>(ev.pos.getY());
                for (std::size_t i = 0; i < logCount_; ++i) {
                    if (logSliders_[i].bounds.contains(mx, my)) {
                        activeLog_ = static_cast<int>(i);
                        dragLog(logSliders_[i], mx);
                        return true;
                    }
                }
            }
        }
        return MagnetoUI::onMouse(ev);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeLog_ >= 0 && activeLog_ < static_cast<int>(logCount_)) {
            dragLog(logSliders_[static_cast<std::size_t>(activeLog_)], static_cast<float>(ev.pos.getX()));
            return true;
        }
        return MagnetoUI::onMotion(ev);
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        logCount_ = 0;
        beginFrame();
        drawHeader("Grist", "granular grain delay",
                   "the input is cut into windowed grains that are shifted, scattered and fed back: Window, Pitch, Blur, Range.");
        const float left = 24.0f, gap = 16.0f, full = width - 48.0f;
        drawGrainPanel({left, 108.0f, 448.0f, 218.0f});
        drawTexturePanel({left + 448.0f + gap, 108.0f, full - 448.0f - gap, 218.0f});
        drawCloudPanel({left, 338.0f, full, 150.0f});
        drawLoopPanel({left, 500.0f, full, 150.0f});
    }

private:
    struct LogSlider {
        std::uint32_t parameter = 0;
        Rect bounds {};
    };

    bool synced(const std::uint32_t syncParam) const { return intValue(syncParam) != 0; }

    float logNorm(const std::uint32_t parameter) const
    {
        const float lo = tables().ranges[parameter].minimum, hi = tables().ranges[parameter].maximum;
        return std::clamp(std::log(std::max(value(parameter), lo) / lo) / std::log(hi / lo), 0.0f, 1.0f);
    }

    void dragLog(const LogSlider& s, const float mouseX)
    {
        const float lo = tables().ranges[s.parameter].minimum, hi = tables().ranges[s.parameter].maximum;
        const float n = std::clamp((mouseX - s.bounds.x) / std::max(1.0f, s.bounds.w), 0.0f, 1.0f);
        commit(s.parameter, lo * std::pow(hi / lo, n));
    }

    void drawLogSlider(const std::uint32_t parameter, const char* name, const char* shown, const Rect b, const Accent accent)
    {
        const auto& t = theme();
        label(b.x, b.y, name);
        fc(t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(b.x + b.w, b.y, shown, nullptr);
        const float trackY = b.y + 17.0f;
        beginPath();
        fc(t.controlTrack);
        roundedRect(b.x, trackY, b.w, 10.0f, 3.0f);
        fill();
        const float n = logNorm(parameter);
        if (n > 0.002f) {
            beginPath();
            fillColor(accent.r, accent.g, accent.b, 255);
            roundedRect(b.x, trackY, b.w * n, 10.0f, 3.0f);
            fill();
        }
        if (logCount_ < logSliders_.size()) logSliders_[logCount_++] = {parameter, {b.x, trackY - 9.0f, b.w, 28.0f}};
    }

    void slider(const std::uint32_t parameter, const char* name, const char* shown, const float x, const float y, const float w,
                const Accent accent)
    {
        drawSlider(parameter, name, shown, {x, y, w, 34.0f}, accent);
    }

    void drawGrainPanel(const Rect b)
    {
        drawPanel(b, "GRAIN", kGrainAccent, "one grain per window / density; each is a windowed slice of the recent input");
        const float colW = (b.w - 28.0f - 16.0f) / 2.0f;
        const float xA = b.x + 14.0f, xB = xA + colW + 16.0f, y0 = b.y + 46.0f;
        char buf[48];

        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kMix) * 100.0f)));
        slider(core::kMix, "Dry / wet", buf, xA, y0, colW, kGrainAccent);

        const bool windowSynced = synced(core::kWindowSync);
        setDim(windowSynced);
        drawLogSlider(core::kWindow, "Window", milliseconds(value(core::kWindow)).c_str(), {xA, y0 + 42.0f, colW, 34.0f}, kGrainAccent);
        setDim(false);

        std::snprintf(buf, sizeof(buf), "%+.1f st", value(core::kPitch));
        slider(core::kPitch, "Pitch", buf, xA, y0 + 84.0f, colW, kGrainAccent);

        std::snprintf(buf, sizeof(buf), "%d per window", intValue(core::kDensity));
        slider(core::kDensity, "Density", buf, xB, y0, colW, kGrainAccent);

        label(xB, y0 + 42.0f, "Window timing", true, 12.0f);
        drawSegments(core::kWindowSync, kTimingNames, 2, {xB, y0 + 60.0f, colW, 26.0f}, kGrainAccent);
        setDim(!windowSynced);
        if (windowSynced)
            std::snprintf(buf, sizeof(buf), "%s = %s", core::divisionName(intValue(core::kWindowDivision)),
                          milliseconds(value(core::kStatusWindow)).c_str());
        else
            std::snprintf(buf, sizeof(buf), "%s", core::divisionName(intValue(core::kWindowDivision)));
        drawStepper(core::kWindowDivision, "Division (host tempo)", buf, {xB, y0 + 94.0f, colW, 40.0f});
        setDim(false);

        char caption[160];
        std::snprintf(caption, sizeof(caption), "average latency %s   (Blur x Window / 2, plus the head start when pitched up)",
                      milliseconds(value(core::kStatusLatency)).c_str());
        label(xA, b.y + b.h - 22.0f, caption, true, 10.5f);
    }

    void drawTexturePanel(const Rect b)
    {
        drawPanel(b, "TEXTURE", kTextureAccent, "per grain, drawn at random from the Seed");
        const float x = b.x + 14.0f, w = b.w - 28.0f, y0 = b.y + 46.0f;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%d%% backwards", static_cast<int>(std::lround(value(core::kReverse) * 100.0f)));
        slider(core::kReverse, "Reverse", buf, x, y0, w, kTextureAccent);
        std::snprintf(buf, sizeof(buf), "up to %.2f windows late", value(core::kBlur));
        slider(core::kBlur, "Blur (random start)", buf, x, y0 + 42.0f, w, kTextureAccent);
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kSpread) * 100.0f)));
        slider(core::kSpread, "Spread (stereo)", buf, x, y0 + 84.0f, w, kTextureAccent);
        std::snprintf(buf, sizeof(buf), "%d", intValue(core::kSeed));
        slider(core::kSeed, "Seed", buf, x, y0 + 126.0f, w, kTextureAccent);
    }

    void drawCloudPanel(const Rect b)
    {
        drawPanel(b, "CLOUD", kCloudAccent, "grain shape, random pitch and random length");
        const float gap = 16.0f;
        const float w = (b.w - 28.0f - 3.0f * gap) / 4.0f;
        const float x0 = b.x + 14.0f, y0 = b.y + 44.0f;
        char buf[48];

        std::snprintf(buf, sizeof(buf), "%d%% percussive", static_cast<int>(std::lround(value(core::kShape) * 100.0f)));
        slider(core::kShape, "Shape", buf, x0, y0, w, kCloudAccent);
        drawShapePlot({x0, y0 + 44.0f, w, b.h - 44.0f - 56.0f});

        const bool ranged = value(core::kRange) > 0.0f;
        std::snprintf(buf, sizeof(buf), "+/- %.1f st", value(core::kRange));
        slider(core::kRange, "Range (random pitch)", buf, x0 + (w + gap), y0, w, kCloudAccent);
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kVariance) * 100.0f)));
        slider(core::kVariance, "Variance (random length)", buf, x0 + (w + gap), y0 + 42.0f, w, kCloudAccent);

        setDim(!ranged);
        drawStepper(core::kScale, "Scale (from the input pitch)", core::scaleName(intValue(core::kScale)),
                    {x0 + 2.0f * (w + gap), y0, w, 40.0f});
        label(x0 + 2.0f * (w + gap), y0 + 46.0f, "Direction", true, 12.0f);
        drawSegments(core::kDirection, kDirections, 3, {x0 + 2.0f * (w + gap), y0 + 64.0f, w, 26.0f}, kCloudAccent);
        setDim(false);

        drawCloudSketch({x0 + 3.0f * (w + gap), y0, w, b.h - 44.0f - 14.0f}, ranged);
    }

    void drawShapePlot(const Rect plot)
    {
        drawPlotFrame(plot);
        const float shape = value(core::kShape);
        const float inner = plot.w - 12.0f, height = plot.h - 12.0f;
        const float x0 = plot.x + 6.0f, base = plot.y + plot.h - 6.0f;
        beginPath();
        moveTo(x0, base);
        for (int i = 0; i <= 48; ++i) {
            const float p = static_cast<float>(i) / 48.0f;
            lineTo(x0 + p * inner, base - core::shapeAt(shape, p) / 1.83f * height);
        }
        lineTo(x0 + inner, base);
        closePath();
        fillColor(kCloudAccent.r, kCloudAccent.g, kCloudAccent.b, 110);
        fill();
        beginPath();
        for (int i = 0; i <= 48; ++i) {
            const float p = static_cast<float>(i) / 48.0f;
            const float py = base - core::shapeAt(shape, p) / 1.83f * height;
            if (i == 0) moveTo(x0, py);
            else lineTo(x0 + p * inner, py);
        }
        strokeColor(kCloudAccent.r, kCloudAccent.g, kCloudAccent.b, 255);
        strokeWidth(1.5f);
        stroke();
        label(plot.x + plot.w * 0.5f, plot.y + plot.h + 4.0f, "one grain's loudness over its length", true, 10.0f, ALIGN_CENTER | ALIGN_TOP);
    }

    // The pitches 16 grains would take: lines are the pitches the Scale allows, red strokes are grains
    // moved off PITCH, and a stroke's length is the grain's size (Variance shortens it).
    void drawCloudSketch(const Rect plot, const bool ranged)
    {
        drawPlotFrame(plot);
        const auto& t = theme();
        const int scale = intValue(core::kScale), direction = intValue(core::kDirection);
        const float range = value(core::kRange), pitch = value(core::kPitch);
        const float lo = direction == core::kUp ? 0.0f : -range, hi = direction == core::kDown ? 0.0f : range;
        const float yMin = std::min(lo, -1.0f), yMax = std::max(hi, 1.0f);
        const Rect a {plot.x + 6.0f, plot.y + 6.0f, plot.w - 12.0f, plot.h - 12.0f};
        const auto yOf = [&](const float o) { return a.y + a.h - (o - yMin) / (yMax - yMin) * a.h; };
        if (ranged && scale > 0) {
            for (int o = static_cast<int>(std::ceil(lo)); o <= static_cast<int>(std::floor(hi)); ++o) {
                if (o == 0 || !core::scaleAllows(scale, static_cast<int>(std::lround(pitch)) + o)) continue;
                beginPath();
                fillColor(t.border.r, t.border.g, t.border.b, 200);
                rect(a.x, std::round(yOf(static_cast<float>(o))), a.w, 1.0f);
                fill();
            }
        }
        beginPath();
        fillColor(t.textDim.r, t.textDim.g, t.textDim.b, 110);
        rect(a.x, std::round(yOf(0.0f)), a.w, 1.0f);
        fill();
        const auto grains = core::cloudPreview(pitch, range, direction, value(core::kVariance), scale, 16,
                                               static_cast<std::uint32_t>(intValue(core::kSeed)));
        const float cell = a.w / static_cast<float>(grains.size());
        for (std::size_t i = 0; i < grains.size(); ++i) {
            beginPath();
            if (grains[i].offset != 0.0f) fillColor(214, 96, 84, 255);
            else fc(t.textPrimary);
            roundedRect(a.x + cell * static_cast<float>(i) + 0.5f, yOf(grains[i].offset) - 1.5f, std::max(1.5f, (cell - 2.0f) * grains[i].size),
                        3.0f, 1.2f);
            fill();
        }
        label(plot.x + plot.w * 0.5f, plot.y + plot.h + 4.0f, "16 grains: pitch against time", true, 10.0f, ALIGN_CENTER | ALIGN_TOP);
    }

    void drawLoopPanel(const Rect b)
    {
        drawPanel(b, "FEEDBACK", kLoopAccent, "the grains go through a delay and an optional filter, then back into the grains");
        const float gap = 16.0f;
        const float w = (b.w - 28.0f - 3.0f * gap) / 4.0f;
        const float x0 = b.x + 14.0f, y0 = b.y + 44.0f;
        char buf[48];

        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kFeedback) * 100.0f)));
        slider(core::kFeedback, "Feedback", buf, x0, y0, w, kLoopAccent);
        label(x0, y0 + 46.0f, "off at 0; held in check by a soft limiter", true, 10.5f);

        const bool loop = value(core::kFeedback) > 0.0f;
        const float x1 = x0 + (w + gap);
        setDim(!loop || synced(core::kDelaySync));
        drawLogSlider(core::kDelay, "Delay", milliseconds(value(core::kDelay)).c_str(), {x1, y0, w, 34.0f}, kLoopAccent);
        setDim(!loop);
        constexpr const char* kShortTiming[2] = {"Free", "Sync"};
        drawSegments(core::kDelaySync, kShortTiming, 2, {x1, y0 + 42.0f, w * 0.42f, 24.0f}, kLoopAccent);
        setDim(!loop || !synced(core::kDelaySync));
        if (synced(core::kDelaySync))
            std::snprintf(buf, sizeof(buf), "%s = %s", core::divisionName(intValue(core::kDelayDivision)),
                          milliseconds(value(core::kStatusDelay)).c_str());
        else
            std::snprintf(buf, sizeof(buf), "%s", core::divisionName(intValue(core::kDelayDivision)));
        drawStepper(core::kDelayDivision, " ", buf, {x1 + w * 0.42f + 8.0f, y0 + 27.0f, w * 0.58f - 8.0f, 40.0f});
        setDim(false);

        const float x2 = x0 + 2.0f * (w + gap);
        const bool filterOn = intValue(core::kFilterOn) != 0;
        setDim(!loop);
        label(x2, y0 - 2.0f, "Filter in the loop", true, 12.0f);
        drawSegments(core::kFilterOn, kOnOffNames, 2, {x2, y0 + 14.0f, w, 26.0f}, kLoopAccent);
        setDim(!loop || !filterOn);
        drawSegments(core::kFilterMode, kFilterModes, 3, {x2, y0 + 48.0f, w, 26.0f}, kLoopAccent);
        setDim(false);

        const float x3 = x0 + 3.0f * (w + gap);
        setDim(!loop || !filterOn);
        std::snprintf(buf, sizeof(buf), "%.0f Hz", core::filterHz(value(core::kFilterFreq)));
        slider(core::kFilterFreq, "Filter frequency", buf, x3, y0, w, kLoopAccent);
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kFilterWidth) * 100.0f)));
        slider(core::kFilterWidth, "Filter width", buf, x3, y0 + 42.0f, w, kLoopAccent);
        setDim(false);
    }

    std::array<LogSlider, 4> logSliders_ {};
    std::size_t logCount_ = 0;
    int activeLog_ = -1;
};

UI* createUI() { return new GristUI(); }

END_NAMESPACE_DISTRHO
