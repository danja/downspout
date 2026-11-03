#include "downspout/dpf/MagnetoKit.hpp"

#include "fresco_core.hpp"
#include "fresco_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace core = downspout::fresco;

namespace {

constexpr kit::Accent kPictureAccent {150, 98, 128};   // plum
constexpr kit::Accent kPlayAccent {92, 140, 156};      // cold steel
constexpr kit::Accent kLookAccent {176, 112, 62};      // copper
constexpr kit::Accent kOutAccent {120, 132, 96};       // sage

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
        for (const std::uint32_t i : {static_cast<std::uint32_t>(core::kLength), static_cast<std::uint32_t>(core::kMinHz), static_cast<std::uint32_t>(core::kMaxHz)})
            ranges[i].logarithmic = true;
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

// Black -> deep purple -> red -> orange -> white: the usual spectrogram palette, so the preview looks like what an analyser will show.
void ramp(const float v, std::uint8_t* rgba)
{
    static constexpr float stops[5][3] = {{0, 0, 0}, {60, 12, 96}, {190, 40, 70}, {250, 160, 40}, {255, 250, 230}};
    const float x = std::clamp(v, 0.0f, 1.0f) * 4.0f;
    const int i = std::min(3, static_cast<int>(x));
    const float f = x - static_cast<float>(i);
    for (int c = 0; c < 3; ++c) rgba[c] = static_cast<std::uint8_t>(stops[i][c] + (stops[i + 1][c] - stops[i][c]) * f);
    rgba[3] = 255;
}

}  // namespace

class FrescoUI : public MagnetoUI
{
public:
    FrescoUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        showPicture(core::defaultPicture());
        // The standalone used for screenshots has no audio: show the playhead part-way across.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            setValue(core::kStatusPlayhead, 0.42f);
            setValue(core::kStatusLevel, 0.6f);
        }
    }

protected:
    enum Action { kLoad = 0, kClear = 1 };

    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_ && core::isOutput(index)) return;   // keep the demo readings
        MagnetoUI::parameterChanged(index, v);
    }

    void stateChanged(const char* key, const char* stateValue) override
    {
        if (std::strcmp(key, core::kStateKeyPicture) != 0) return;
        path_ = stateValue != nullptr ? stateValue : "";
        failed_ = false;
        error_.clear();
        if (path_.empty()) {
            showPicture(core::defaultPicture());
        } else {
            core::DecodeResult r = core::loadImageFile(path_);
            if (r.ok) {
                showPicture(r.image);
            } else {
                failed_ = true;
                error_ = r.error;
                showPicture(core::defaultPicture());
            }
        }
        repaint();
    }

    void onAction(const int id) override
    {
        if (id == kLoad) {
            requestStateFile(core::kStateKeyPicture);
        } else if (id == kClear) {
            path_.clear();
            failed_ = false;
            error_.clear();
            showPicture(core::defaultPicture());
            setState(core::kStateKeyPicture, "");
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("FRESCO", "spectrogram painter",
                   "Load a picture and it is painted into the sound: height is frequency, brightness is level, and a playhead sweeps left to right. Feed it audio and watch a spectrogram.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawPicture({24.0f, 108.0f, full, 250.0f});
        const float gap = 12.0f, y = 370.0f, h = 216.0f;
        const float w1 = 276.0f, w3 = 232.0f, w2 = full - w1 - w3 - 2.0f * gap;
        drawPlayback({24.0f, y, w1, h});
        drawLook({24.0f + w1 + gap, y, w2, h});
        drawOutput({24.0f + w1 + w2 + 2.0f * gap, y, w3, h});
    }

private:
    static std::string basename(const std::string& path)
    {
        const auto pos = path.find_last_of("/\\");
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    void showPicture(const core::Image& image)
    {
        const core::Image small = core::reduced(image, 640);
        pixelWidth_ = small.width;
        pixelHeight_ = small.height;
        sourceWidth_ = image.width;
        sourceHeight_ = image.height;
        rgba_.assign(static_cast<std::size_t>(small.width) * static_cast<std::size_t>(small.height) * 4, 0);
        for (std::size_t i = 0; i < small.luma.size(); ++i) ramp(static_cast<float>(small.luma[i]) / 255.0f, &rgba_[i * 4]);
        dirty_ = true;
    }

    void drawPicture(const Rect b)
    {
        const bool custom = !path_.empty() && !failed_;
        drawPanel(b, "PICTURE", kPictureAccent, custom ? "your picture" : "built-in picture");
        const auto& t = theme();
        const float pad = 14.0f, left = 232.0f;

        const std::string name = custom ? basename(path_) : (failed_ ? "Could not load this file" : "Rings and cross");
        std::string shown = name.size() > 28 ? name.substr(0, 25) + "..." : name;
        fc(failed_ ? t.textPrimary : (custom ? t.textPrimary : t.textDim));
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(b.x + pad, b.y + 44.0f, shown.c_str(), nullptr);
        char line[96];
        if (failed_) {
            std::string e = error_.size() > 40 ? error_.substr(0, 37) + "..." : error_;
            label(b.x + pad, b.y + 68.0f, e.c_str(), false, 11.0f, ALIGN_LEFT | ALIGN_TOP);
            label(b.x + pad, b.y + 84.0f, "The built-in picture is used.", true, 11.0f, ALIGN_LEFT | ALIGN_TOP);
        } else {
            std::snprintf(line, sizeof(line), "%d x %d pixels", sourceWidth_, sourceHeight_);
            label(b.x + pad, b.y + 68.0f, line, false, 12.0f, ALIGN_LEFT | ALIGN_TOP);
        }
        drawButton(kLoad, custom ? "Replace picture" : "Load picture", {b.x + pad, b.y + 112.0f, 150.0f, 28.0f}, kPictureAccent);
        if (!path_.empty() || failed_) drawButton(kClear, "Clear", {b.x + pad + 158.0f, b.y + 112.0f, 60.0f, 28.0f}, kPictureAccent, false);
        label(b.x + pad, b.y + 160.0f, "PNG, PGM/PPM or BMP.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(b.x + pad, b.y + 176.0f, "JPEG and video are not supported yet.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(b.x + pad, b.y + 192.0f, "Top of the picture = high frequency.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(b.x + pad, b.y + 208.0f, "Left of the picture = start of the sweep.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        const Rect plot {b.x + left, b.y + 40.0f, b.w - left - pad, b.h - 54.0f};
        drawPlotFrame(plot);
        if (dirty_ && !rgba_.empty()) {
            image_ = createImageFromRGBA(static_cast<uint>(pixelWidth_), static_cast<uint>(pixelHeight_), rgba_.data(), 0);
            dirty_ = false;
        }
        if (image_.isValid()) {
            beginPath();
            rect(plot.x, plot.y, plot.w, plot.h);
            fillPaint(imagePattern(plot.x, plot.y, plot.w, plot.h, 0.0f, image_, 1.0f));
            fill();
        }
        // A dimmed picture shows what the range and contrast settings do to the loudness of the dark areas: the playhead column is the live part.
        const float head = std::clamp(value(core::kStatusPlayhead), 0.0f, 1.0f);
        beginPath();
        strokeColor(255, 255, 255, 230);
        strokeWidth(2.0f);
        moveTo(plot.x + plot.w * head, plot.y);
        lineTo(plot.x + plot.w * head, plot.y + plot.h);
        stroke();
        char hz[32];
        std::snprintf(hz, sizeof(hz), "%.0f Hz", static_cast<double>(value(core::kMaxHz)));
        label(plot.x + 6.0f, plot.y + 10.0f, hz, false, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(hz, sizeof(hz), "%.0f Hz", static_cast<double>(value(core::kMinHz)));
        label(plot.x + 6.0f, plot.y + plot.h - 10.0f, hz, false, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(hz, sizeof(hz), "%.1f s", static_cast<double>(value(core::kLength)));
        label(plot.x + plot.w - 6.0f, plot.y + plot.h - 10.0f, hz, false, 11.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawPlayback(const Rect b)
    {
        const bool host = intValue(core::kTransport) == 1;
        drawPanel(b, "PLAYBACK", kPlayAccent, host ? "locked to the song position" : "free-running loop");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.1f s", static_cast<double>(value(core::kLength)));
        drawSlider(core::kLength, "Length (one pass of the picture)", buf, {b.x + pad, b.y + 38.0f, w, 28.0f}, kPlayAccent);
        static constexpr const char* kClock[2] = {"Free loop", "Follow host"};
        label(b.x + pad, b.y + 88.0f, "Playhead", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kTransport, kClock, 2, {b.x + pad + 66.0f, b.y + 76.0f, w - 66.0f, 24.0f}, kPlayAccent);
        static constexpr const char* kSource[2] = {"Input", "Tone"};
        label(b.x + pad, b.y + 124.0f, "Source", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kSource, kSource, 2, {b.x + pad + 66.0f, b.y + 112.0f, w - 66.0f, 24.0f}, kPlayAccent);
        const bool tone = intValue(core::kSource) == 1;
        label(b.x + pad, b.y + 148.0f, tone ? "Tone: a clean sinusoid bank, steady and bright." : "Input: keeps the input's pitch and timing.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        std::snprintf(buf, sizeof(buf), "%.0f %%", static_cast<double>(value(core::kFollow) * 100.0f));
        drawSlider(core::kFollow, "Follow input level (0 = drone)", buf, {b.x + pad, b.y + 164.0f, w, 28.0f}, kPlayAccent);
    }

    void drawLook(const Rect b)
    {
        drawPanel(b, "MAPPING", kLookAccent, "pixels to sound");
        const float pad = 14.0f, w = b.w - 2.0f * pad, half = (w - 12.0f) * 0.5f;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(value(core::kMinHz)));
        drawSlider(core::kMinHz, "Bottom of picture", buf, {b.x + pad, b.y + 38.0f, half, 28.0f}, kLookAccent);
        std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(value(core::kMaxHz)));
        drawSlider(core::kMaxHz, "Top of picture", buf, {b.x + pad + half + 12.0f, b.y + 38.0f, half, 28.0f}, kLookAccent);
        std::snprintf(buf, sizeof(buf), "%.0f dB", static_cast<double>(value(core::kRange)));
        drawSlider(core::kRange, "Range", buf, {b.x + pad, b.y + 86.0f, half, 28.0f}, kLookAccent);
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value(core::kContrast)));
        drawSlider(core::kContrast, "Contrast", buf, {b.x + pad + half + 12.0f, b.y + 86.0f, half, 28.0f}, kLookAccent);
        static constexpr const char* kAxis[2] = {"Linear", "Log"};
        static constexpr const char* kPol[2] = {"Bright = loud", "Dark = loud"};
        label(b.x + pad, b.y + 144.0f, "Axis", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kLogAxis, kAxis, 2, {b.x + pad + 48.0f, b.y + 132.0f, w - 48.0f, 24.0f}, kLookAccent);
        label(b.x + pad, b.y + 176.0f, "Pixels", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kInvert, kPol, 2, {b.x + pad + 48.0f, b.y + 164.0f, w - 48.0f, 24.0f}, kLookAccent);
    }

    void drawOutput(const Rect b)
    {
        drawPanel(b, "OUTPUT", kOutAccent, "wet and dry");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.0f %%", static_cast<double>(value(core::kMix) * 100.0f));
        drawSlider(core::kMix, "Mix (dry to painted)", buf, {b.x + pad, b.y + 38.0f, w, 28.0f}, kOutAccent);
        std::snprintf(buf, sizeof(buf), "%+.1f dB", static_cast<double>(value(core::kOutput)));
        drawSlider(core::kOutput, "Output", buf, {b.x + pad, b.y + 86.0f, w, 28.0f}, kOutAccent);
        label(b.x + pad, b.y + 140.0f, "Latency 2048 samples,", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(b.x + pad, b.y + 156.0f, "reported to the host.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(b.x + pad, b.y + 180.0f, "Brighter pictures are louder:", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(b.x + pad, b.y + 196.0f, "trim with Output.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    std::string path_, error_;
    bool failed_ = false, demo_ = false, dirty_ = false;
    int pixelWidth_ = 0, pixelHeight_ = 0, sourceWidth_ = 0, sourceHeight_ = 0;
    std::vector<std::uint8_t> rgba_;
    NanoImage image_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrescoUI)
};

UI* createUI()
{
    return new FrescoUI();
}

END_NAMESPACE_DISTRHO
