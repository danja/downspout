#include "MagnetoKit.hpp"

#include "retune_params.hpp"
#include "retune_scale.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::retune;

namespace {

constexpr kit::Accent kScaleAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kMapAccent {176, 112, 62};     // copper
constexpr kit::Accent kTuningAccent {198, 132, 58};  // amber

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

std::string noteName(const int midi)
{
    static constexpr const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%s%d (%d)", kNames[(midi % 12 + 12) % 12], midi / 12 - 1, midi);
    return buffer;
}

}  // namespace

class RetuneUI : public MagnetoUI {
public:
    RetuneUI()
        : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        scale_ = core::equalTemperament();
    }

protected:
    enum Action { kLoad = 0, kClear = 1 };

    void stateChanged(const char* key, const char* stateValue) override
    {
        if (std::strcmp(key, "scale_file") != 0)
            return;
        path_ = stateValue != nullptr ? stateValue : "";
        failed_ = false;
        scale_ = core::equalTemperament();
        if (!path_.empty()) {
            if (auto loaded = core::loadSclFile(path_))
                scale_ = std::move(*loaded);
            else
                failed_ = true;
        }
        repaint();
    }

    void onAction(const int id) override
    {
        if (id == kLoad) {
            requestStateFile("scale_file");
        } else if (id == kClear) {
            path_.clear();
            failed_ = false;
            scale_ = core::equalTemperament();
            setState("scale_file", "");
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        beginFrame();
        drawHeader("Retune", "Scala microtuning for MIDI",
                   "notes -> nearest 12-TET key + a pitch bend, one channel per note -> a synth with per-channel bend.");
        drawScalePanel({24.0f, 108.0f, width - 48.0f, 150.0f});
        drawMapPanel({24.0f, 270.0f, width - 48.0f, 192.0f});
        drawTuningPanel({24.0f, 474.0f, width - 48.0f, 120.0f});
    }

private:
    static std::string basename(const std::string& path)
    {
        const auto pos = path.find_last_of("/\\");
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    void drawScalePanel(const Rect b)
    {
        drawPanel(b, "SCALE", kScaleAccent, ".scl file, read outside audio processing");
        const bool custom = !path_.empty() && !failed_;
        const auto& t = theme();

        const std::string name = path_.empty() ? "12-tone equal temperament (no file loaded)" : basename(path_);
        fc(custom ? t.textPrimary : t.textDim);
        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(b.x + 14.0f, b.y + 44.0f, name.c_str(), nullptr);

        if (failed_)
            label(b.x + 14.0f, b.y + 68.0f, "Could not read this file as a Scala scale. Using 12-TET.", false);
        else if (!path_.empty())
            label(b.x + 14.0f, b.y + 68.0f, scale_.description.c_str());

        char line[128];
        std::snprintf(line, sizeof(line), "%d degrees per period  -  period %.2f cents", scale_.size(), scale_.period());
        label(b.x + 14.0f, b.y + 88.0f, line);

        drawButton(kLoad, custom ? "Replace .scl" : "Load .scl", {b.x + 14.0f, b.y + 110.0f, 150.0f, 28.0f}, kScaleAccent);
        if (!path_.empty())
            drawButton(kClear, "Clear", {b.x + 174.0f, b.y + 110.0f, 90.0f, 28.0f}, kScaleAccent, false);
    }

    // One period left to right: the 12-TET keys above, the scale's degrees below.
    void drawMapPanel(const Rect b)
    {
        drawPanel(b, "SCALE MAP", kMapAccent, "one period, left to right");
        const auto& t = theme();
        const float left = b.x + 26.0f, right = b.x + b.w - 26.0f;
        const float period = static_cast<float>(scale_.period());
        const auto px = [&](const double cents) { return left + static_cast<float>(cents / period) * (right - left); };

        drawPlotFrame({b.x + 14.0f, b.y + 44.0f, b.w - 28.0f, b.h - 58.0f});
        label(left, b.y + 52.0f, "12-TET keys", true, 10.0f);
        for (int c = 0; c * 100.0 <= period + 0.5; ++c) {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(px(c * 100.0), b.y + 68.0f);
            lineTo(px(c * 100.0), b.y + 96.0f);
            stroke();
        }

        label(left, b.y + 106.0f, "scale degrees, cents above the root", true, 10.0f);
        const int count = scale_.size();
        const bool labelled = count <= 24;
        for (int i = 0; i <= count; ++i) {
            const double cents = i == 0 ? 0.0 : scale_.cents[static_cast<std::size_t>(i - 1)];
            const bool edge = i == 0 || i == count;
            beginPath();
            strokeColor(kMapAccent.r, kMapAccent.g, kMapAccent.b, 255);
            strokeWidth(edge ? 3.0f : 2.0f);
            moveTo(px(cents), b.y + 124.0f);
            lineTo(px(cents), b.y + 150.0f);
            stroke();
            if (labelled) {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%.0f", cents);
                label(px(cents), b.y + 154.0f + ((i % 2) ? 12.0f : 0.0f), buf, true, 10.0f, ALIGN_CENTER | ALIGN_TOP);
            }
        }
        if (!labelled)
            label(left, b.y + 156.0f, "More than 24 degrees: cent values are not labelled.", true, 10.0f);
    }

    void drawTuningPanel(const Rect b)
    {
        drawPanel(b, "TUNING", kTuningAccent, "MIDI out: channels 2-16, one note per channel");
        const float gap = 20.0f;
        const float w = (b.w - 28.0f - 2.0f * gap) / 3.0f;
        const float x = b.x + 14.0f, y = b.y + 46.0f;

        const std::string root = noteName(intValue(core::kRoot));
        drawSlider(core::kRoot, "Root note (plays 1/1)", root.c_str(), {x, y, w, 34.0f}, kTuningAccent);
        char range[24];
        std::snprintf(range, sizeof(range), "%d semitones", intValue(core::kBendRange));
        drawSlider(core::kBendRange, "Bend range", range, {x + w + gap, y, w, 34.0f}, kTuningAccent);

        char notes[24];
        std::snprintf(notes, sizeof(notes), "%d sounding", intValue(core::kStatusNotes));
        label(x + 2.0f * (w + gap), y, "Output");
        label(x + 2.0f * (w + gap), y + 17.0f, notes, false, 14.0f);

        char msg[96];
        std::snprintf(msg, sizeof(msg), "The synth needs per-channel pitch bend set to +/-%d semitones.",
                      intValue(core::kBendRange));
        label(x, y + 56.0f, msg);
    }

    std::string path_;
    bool failed_ = false;
    core::Scale scale_;
};

UI* createUI() { return new RetuneUI(); }

END_NAMESPACE_DISTRHO
