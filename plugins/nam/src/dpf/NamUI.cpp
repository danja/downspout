#include "downspout/dpf/MagnetoKit.hpp"

#include "nam_engine.hpp"
#include "nam_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::namfx;

namespace {

constexpr kit::Accent kModelAccent {176, 84, 58};   // amp red
constexpr kit::Accent kToneAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kCabAccent {132, 110, 62};    // speaker cloth
constexpr kit::Accent kOutAccent {82, 132, 100};    // green
constexpr kit::Accent kPlotAccent {198, 132, 58};   // amber

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

std::string basename(const std::string& path)
{
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

}  // namespace

class NamUI : public MagnetoUI {
public:
    NamUI()
        : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
    }

protected:
    enum Action { kLoadModel = 0, kClearModel = 1, kLoadIr = 2, kClearIr = 3 };

    void stateChanged(const char* key, const char* stateValue) override
    {
        const std::string v = stateValue != nullptr ? stateValue : "";
        if (std::strcmp(key, "model_file") == 0)
            modelPath_ = v;
        else if (std::strcmp(key, "ir_file") == 0)
            irPath_ = v;
        repaint();
    }

    void onAction(const int id) override
    {
        switch (id) {
        case kLoadModel: requestStateFile("model_file"); break;
        case kClearModel:
            modelPath_.clear();
            setState("model_file", "");
            break;
        case kLoadIr: requestStateFile("ir_file"); break;
        case kClearIr:
            irPath_.clear();
            setState("ir_file", "");
            break;
        default: break;
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float half = (width - 48.0f - 12.0f) * 0.5f;
        beginFrame();
        drawHeader("NAM", "Neural amp modeler + cabinet IR",
                   "input -> gate -> .nam model -> tone -> cabinet IR -> output. Load a model and an impulse response to start.");
        drawModelPanel({24.0f, 108.0f, half, 176.0f});
        drawTonePanel({24.0f + half + 12.0f, 108.0f, half, 176.0f});
        drawCabinetPanel({24.0f, 296.0f, half, 176.0f});
        drawOutputPanel({24.0f + half + 12.0f, 296.0f, half, 176.0f});
        drawResponsePanel({24.0f, 484.0f, width - 48.0f, 156.0f});
    }

private:
    static std::string db(const float v) { return fmt("%+.1f dB", v); }
    static std::string fmt(const char* format, const float v)
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), format, static_cast<double>(v));
        return buf;
    }
    static std::string hz(const float v)
    {
        char buf[32];
        if (v >= 1000.0f)
            std::snprintf(buf, sizeof(buf), "%.1f kHz", static_cast<double>(v) / 1000.0);
        else
            std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(v));
        return buf;
    }

    // File name, status line, and one row of controls: Load, Clear and the stage's two switches.
    void drawFileRow(const Rect b, const std::string& path, const int status, const char* emptyText, const std::string& info,
                     const int loadId, const int clearId, const char* loadLabel, const std::uint32_t enableParam,
                     const char* enableLabel, const std::uint32_t levelParam, const kit::Accent accent)
    {
        const auto& t = theme();
        const bool failed = status == core::kSlotError;
        const std::string name = path.empty() ? emptyText : basename(path);
        fc(failed ? laf::Colour {200, 70, 60, 255} : (path.empty() ? t.textDim : t.textPrimary));
        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(b.x, b.y, name.c_str(), nullptr);
        label(b.x, b.y + 22.0f, info.c_str(), !failed);

        const float y = b.y + 46.0f;
        drawButton(loadId, path.empty() ? loadLabel : "Replace", {b.x, y, 96.0f, 26.0f}, accent);
        if (!path.empty())
            drawButton(clearId, "Clear", {b.x + 104.0f, y, 58.0f, 26.0f}, accent, false);

        static constexpr const char* kActive[] = {"Bypass", "Active"};
        static constexpr const char* kLoudness[] = {"Raw", "-18 dB"};
        const float segX = b.x + 176.0f;
        const float segW = (b.w - 176.0f - 10.0f) * 0.5f;
        label(segX, y - 14.0f, enableLabel, true, 10.0f);
        drawSegments(enableParam, kActive, 2, {segX, y, segW, 26.0f}, accent);
        if (levelParam != core::kParameterCount) {
            label(segX + segW + 10.0f, y - 14.0f, "Loudness", true, 10.0f);
            drawSegments(levelParam, kLoudness, 2, {segX + segW + 10.0f, y, segW, 26.0f}, accent);
        }
    }

    void drawModelPanel(const Rect b)
    {
        drawPanel(b, "MODEL", kModelAccent, "Neural Amp Modeler .nam file");
        const float x = b.x + 14.0f, y = b.y + 42.0f, w = b.w - 28.0f;
        const int status = intValue(core::kStatusModel);

        std::string info;
        const float rate = value(core::kModelRate);
        if (status == core::kSlotError) {
            info = "Could not read this file as a model";
        } else if (status == core::kSlotLoaded) {
            char line[96];
            const double host = getSampleRate();
            if (std::fabs(static_cast<double>(rate) - host) > 0.5)
                std::snprintf(line, sizeof(line), "Runs at %.0f Hz, resampled from the host's %.0f Hz", static_cast<double>(rate), host);
            else
                std::snprintf(line, sizeof(line), "Runs at %.0f Hz", static_cast<double>(rate));
            info = line;
        }
        drawFileRow({x, y, w, 70.0f}, modelPath_, status, "No model loaded - the signal passes through", info, kLoadModel,
                    kClearModel, "Load .nam", core::kModelEnable, "Model", core::kNormalize, kModelAccent);

        const float gap = 16.0f;
        const float sw = (w - gap) * 0.5f;
        drawSlider(core::kInputGain, "Input gain", db(value(core::kInputGain)).c_str(), {x, y + 92.0f, sw, 34.0f}, kModelAccent);
        const float gate = value(core::kGate);
        drawSlider(core::kGate, "Noise gate", gate <= core::kGateOff + 0.5f ? "Off" : db(gate).c_str(),
                   {x + sw + gap, y + 92.0f, sw, 34.0f}, kModelAccent);
    }

    void drawTonePanel(const Rect b)
    {
        drawPanel(b, "TONE", kToneAccent, "after the model, before the cabinet");
        const float x = b.x + 14.0f, y = b.y + 46.0f, w = b.w - 28.0f;
        drawSlider(core::kBass, "Bass  (120 Hz shelf)", db(value(core::kBass)).c_str(), {x, y, w, 34.0f}, kToneAccent);
        drawSlider(core::kMid, "Mid  (750 Hz)", db(value(core::kMid)).c_str(), {x, y + 40.0f, w, 34.0f}, kToneAccent);
        drawSlider(core::kTreble, "Treble  (3.5 kHz shelf)", db(value(core::kTreble)).c_str(), {x, y + 80.0f, w, 34.0f},
                   kToneAccent);
    }

    void drawCabinetPanel(const Rect b)
    {
        drawPanel(b, "CABINET", kCabAccent, "impulse response .wav, mono or stereo, up to 2 s");
        const float x = b.x + 14.0f, y = b.y + 42.0f, w = b.w - 28.0f;
        const int status = intValue(core::kStatusIr);

        std::string info;
        if (status == core::kSlotError)
            info = "Could not read this file as a WAV impulse response";
        else if (status == core::kSlotLoaded)
            info = std::to_string(intValue(core::kIrMs)) + " ms long";
        drawFileRow({x, y, w, 70.0f}, irPath_, status, "No impulse response loaded", info, kLoadIr, kClearIr, "Load .wav",
                    core::kIrEnable, "Cabinet", core::kParameterCount, kCabAccent);

        const float gap = 14.0f;
        const float sw = (w - 2.0f * gap) / 3.0f;
        const float sy = y + 92.0f;
        drawSlider(core::kIrLowCut, "Low cut", value(core::kIrLowCut) <= core::kLowCutOff * 1.001f ? "Off" : hz(value(core::kIrLowCut)).c_str(),
                   {x, sy, sw, 34.0f}, kCabAccent);
        drawSlider(core::kIrHighCut, "High cut", value(core::kIrHighCut) >= core::kHighCutOff * 0.999f ? "Off" : hz(value(core::kIrHighCut)).c_str(),
                   {x + sw + gap, sy, sw, 34.0f}, kCabAccent);
        drawSlider(core::kIrGain, "IR gain", db(value(core::kIrGain)).c_str(), {x + 2.0f * (sw + gap), sy, sw, 34.0f}, kCabAccent);
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "OUTPUT", kOutAccent, "level, dry/wet and channel handling");
        const float x = b.x + 14.0f, y = b.y + 46.0f, w = b.w - 28.0f;
        const float gap = 16.0f;
        const float sw = (w - gap) * 0.5f;
        drawSlider(core::kOutputGain, "Output gain", db(value(core::kOutputGain)).c_str(), {x, y, sw, 34.0f}, kOutAccent);
        drawSlider(core::kMix, "Dry / wet", fmt("%.0f %% wet", value(core::kMix) * 100.0f).c_str(), {x + sw + gap, y, sw, 34.0f},
                   kOutAccent);

        static constexpr const char* kChannels[] = {"Mono sum", "Stereo"};
        label(x, y + 52.0f, "Channels (stereo runs one model per side)", true, 10.0f);
        drawSegments(core::kChannels, kChannels, 2, {x, y + 68.0f, sw, 26.0f}, kOutAccent);
        drawMeter(x + sw + gap, y + 62.0f, sw, "IN", value(core::kInLevel));
        drawMeter(x + sw + gap, y + 82.0f, sw, "OUT", value(core::kOutLevel));
    }

    void drawMeter(const float x, const float y, const float w, const char* name, const float levelDb)
    {
        const auto& t = theme();
        label(x, y, name, true, 11.0f);
        const float bx = x + 34.0f, bw = w - 34.0f;
        beginPath();
        fc(t.controlTrack);
        roundedRect(bx, y + 2.0f, bw, 8.0f, 3.0f);
        fill();
        const float n = std::clamp((levelDb + 60.0f) / 66.0f, 0.0f, 1.0f);
        if (n > 0.002f) {
            beginPath();
            if (levelDb > -0.5f)
                fillColor(200, 70, 60, 255);
            else
                fillColor(kOutAccent.r, kOutAccent.g, kOutAccent.b, 255);
            roundedRect(bx, y + 2.0f, bw * n, 8.0f, 3.0f);
            fill();
        }
        // 0 dBFS tick.
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        moveTo(bx + bw * (60.0f / 66.0f), y);
        lineTo(bx + bw * (60.0f / 66.0f), y + 12.0f);
        stroke();
    }

    // The tone controls and the cabinet cuts, on one log-frequency axis. The impulse response
    // itself is not drawn: the panel does not read the .wav file.
    void drawResponsePanel(const Rect b)
    {
        drawPanel(b, "RESPONSE", kPlotAccent, "tone + cabinet cuts, not the model or the IR itself");
        const auto& t = theme();
        const Rect plot {b.x + 14.0f, b.y + 44.0f, b.w - 28.0f, b.h - 58.0f};
        drawPlotFrame(plot);

        constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f, kRangeDb = 15.0f;
        const auto px = [&](const float f) {
            return plot.x + std::log(f / kMinHz) / std::log(kMaxHz / kMinHz) * plot.w;
        };
        const auto py = [&](const float d) {
            return plot.y + plot.h * 0.5f - std::clamp(d, -kRangeDb, kRangeDb) / kRangeDb * plot.h * 0.5f;
        };

        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        moveTo(plot.x, py(0.0f));
        lineTo(plot.x + plot.w, py(0.0f));
        for (const float f : {100.0f, 1000.0f, 10000.0f}) {
            moveTo(px(f), plot.y);
            lineTo(px(f), plot.y + plot.h);
        }
        stroke();
        label(px(100.0f) + 3.0f, plot.y + plot.h - 14.0f, "100", true, 10.0f);
        label(px(1000.0f) + 3.0f, plot.y + plot.h - 14.0f, "1k", true, 10.0f);
        label(px(10000.0f) + 3.0f, plot.y + plot.h - 14.0f, "10k", true, 10.0f);
        label(plot.x + 4.0f, py(0.0f) - 14.0f, "0 dB", true, 10.0f);
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        for (const float d : {-12.0f, -6.0f, 6.0f, 12.0f}) {
            moveTo(plot.x, py(d));
            lineTo(plot.x + plot.w, py(d));
        }
        stroke();
        label(plot.x + 4.0f, py(12.0f) - 14.0f, "+12", true, 10.0f);
        label(plot.x + 4.0f, py(-12.0f) + 2.0f, "-12", true, 10.0f);

        constexpr double kSr = 48000.0;
        const core::ToneCoeffs tone = core::designTone(kSr, value(core::kBass), value(core::kMid), value(core::kTreble));
        const bool lowOn = value(core::kIrLowCut) > core::kLowCutOff * 1.001f;
        const bool highOn = value(core::kIrHighCut) < core::kHighCutOff * 0.999f;
        const core::BiquadCoeffs low = core::designBiquad(core::FilterType::HighPass, kSr, value(core::kIrLowCut), 0.7071, 0.0);
        const core::BiquadCoeffs high = core::designBiquad(core::FilterType::LowPass, kSr, value(core::kIrHighCut), 0.7071, 0.0);

        beginPath();
        strokeColor(kPlotAccent.r, kPlotAccent.g, kPlotAccent.b, 255);
        strokeWidth(2.0f);
        constexpr int kPoints = 160;
        for (int i = 0; i < kPoints; ++i) {
            const float f = kMinHz * std::pow(kMaxHz / kMinHz, static_cast<float>(i) / (kPoints - 1));
            double d = core::toneResponseDb(tone, f, kSr);
            if (lowOn)
                d += 20.0 * std::log10(std::max(core::biquadMagnitude(low, f, kSr), 1.0e-6));
            if (highOn)
                d += 20.0 * std::log10(std::max(core::biquadMagnitude(high, f, kSr), 1.0e-6));
            const float x = px(f), y = py(static_cast<float>(d));
            if (i == 0)
                moveTo(x, y);
            else
                lineTo(x, y);
        }
        stroke();
    }

    std::string modelPath_, irPath_;
};

UI* createUI() { return new NamUI(); }

END_NAMESPACE_DISTRHO
