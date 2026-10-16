#include "downspout/dpf/MagnetoKit.hpp"

#include "trajectory_oscillator.hpp"
#include "trajectory_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::trajectory;

namespace {

constexpr kit::Accent kOscillatorAccent {198, 132, 58};  // amber (magneto DRIVE)
constexpr kit::Accent kOrbitAccent {176, 88, 62};        // hot metal (magneto ENGINE)
constexpr kit::Accent kEnvelopeAccent {92, 140, 156};    // cold steel (magneto EXHAUST)
constexpr kit::Accent kOutputAccent {124, 148, 104};     // olive (magneto LISTEN)

constexpr float kPi = 3.14159265358979323846f;

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
        {
            const auto& spec = core::kParameterSpecs[i];
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

constexpr std::uint32_t idx(const core::ParamId id)
{
    return static_cast<std::uint32_t>(id);
}

std::string formatValue(const core::ParamId id, const float v)
{
    char buffer[48];
    switch (id)
    {
    case core::ParamId::sides:
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
        break;
    case core::ParamId::startPosition:
    case core::ParamId::launchAngle:
        std::snprintf(buffer, sizeof(buffer), "%d deg", static_cast<int>(std::lround(v)));
        break;
    case core::ParamId::bounceJitter:
        std::snprintf(buffer, sizeof(buffer), "%.1f deg", static_cast<double>(v));
        break;
    case core::ParamId::attack:
    case core::ParamId::decay:
    case core::ParamId::release:
        if (v < 1000.0f)
            std::snprintf(buffer, sizeof(buffer), "%d ms", static_cast<int>(std::lround(v)));
        else
            std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(v) / 1000.0);
        break;
    default:
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 100.0f)));
        break;
    }
    return buffer;
}

// Where a time sits on a log axis between the parameter's limits, 0..1.
float logPosition(const core::ParamId id, const float ms)
{
    const auto& spec = core::kParameterSpecs[idx(id)];
    const float lo = std::log(spec.minimum);
    const float hi = std::log(spec.maximum);
    return std::clamp((std::log(std::max(ms, spec.minimum)) - lo) / (hi - lo), 0.0f, 1.0f);
}

}  // namespace

class TrajectoryUI : public MagnetoUI {
public:
    TrajectoryUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) {}

protected:
    void onAction(int) override {}

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        beginFrame();
        drawHeader("Trajectory", "polygon bounce oscillator",
                   "a point bounces inside a regular polygon; its x and y position, mixed, is the waveform.");

        const float gap = 16.0f;
        const float leftW = 560.0f;
        const float rightX = 24.0f + leftW + gap;
        const float rightW = width - 24.0f - rightX;

        drawOscillatorPanel({24.0f, 108.0f, leftW, 280.0f});
        drawOrbitPanel({rightX, 108.0f, rightW, 280.0f});
        drawEnvelopePanel({24.0f, 404.0f, leftW, 216.0f});
        drawOutputPanel({rightX, 404.0f, rightW, 216.0f});
    }

private:
    void slider(const core::ParamId id, const char* name, const Rect b, const Accent accent)
    {
        drawSlider(idx(id), name, formatValue(id, value(idx(id))).c_str(), b, accent);
    }

    void drawOscillatorPanel(const Rect b)
    {
        drawPanel(b, "OSCILLATOR", kOscillatorAccent, "CC 1-6, the numbers Drift's lanes emit");

        const float colW = (b.w - 28.0f - 24.0f) / 2.0f;
        const float colA = b.x + 14.0f;
        const float colB = colA + colW + 24.0f;
        const float top = b.y + 46.0f;
        const float rowH = 46.0f;

        slider(core::ParamId::sides, "Sides  (CC 1)", {colA, top, colW, rowH}, kOscillatorAccent);
        slider(core::ParamId::startPosition, "Start Position  (CC 2)", {colA, top + rowH, colW, rowH}, kOscillatorAccent);
        slider(core::ParamId::launchAngle, "Launch Angle  (CC 3)", {colA, top + 2.0f * rowH, colW, rowH}, kOscillatorAccent);

        slider(core::ParamId::bounceJitter, "Bounce Jitter  (CC 4)", {colB, top, colW, rowH}, kOscillatorAccent);
        slider(core::ParamId::mixX, "X Level  (CC 5)", {colB, top + rowH, colW, rowH}, kOscillatorAccent);
        slider(core::ParamId::mixY, "Y Level  (CC 6)", {colB, top + 2.0f * rowH, colW, rowH}, kOscillatorAccent);

        const float noteY = top + 3.0f * rowH + 6.0f;
        label(colA, noteY, "Sides: 3 is a triangle, 12 nearly a circle. Start Position is where the point begins on the", true, 10.5f);
        label(colA, noteY + 14.0f, "edge; Launch Angle is the way it sets off. Jitter nudges every bounce by up to that many", true, 10.5f);
        label(colA, noteY + 28.0f, "degrees. X and Y Level mix the point's two coordinates; Y alone is the default.", true, 10.5f);
    }

    // The orbit the oscillator would trace, run through the same core class the
    // synth uses, at a fixed on-screen speed rather than the played pitch.
    void drawOrbitPanel(const Rect b)
    {
        drawPanel(b, "ORBIT", kOrbitAccent, "preview of the path and the wave it makes");

        const int sides = intValue(idx(core::ParamId::sides));
        const float startPosition = value(idx(core::ParamId::startPosition));
        const float launchAngle = value(idx(core::ParamId::launchAngle));
        const float jitter = value(idx(core::ParamId::bounceJitter));
        const float mixX = value(idx(core::ParamId::mixX));
        const float mixY = value(idx(core::ParamId::mixY));

        const float plotSize = 170.0f;
        const Rect plot {b.x + (b.w - plotSize) * 0.5f, b.y + 42.0f, plotSize, plotSize};
        drawPlotFrame(plot);
        const float cx = plot.x + plot.w * 0.5f;
        const float cy = plot.y + plot.h * 0.5f;
        const float radius = plot.w * 0.5f - 12.0f;

        // Simulate.
        constexpr int kSteps = 640;
        constexpr double kPreviewSpeed = 0.045;
        core::Oscillator osc;
        osc.setSampleRate(48000.0);
        osc.setSides(sides);
        osc.setBounceJitter(static_cast<double>(jitter) * (kPi / 180.0));
        osc.setFrequency(kPreviewSpeed * 48000.0 / core::Oscillator::kSpeedPerHertz);
        osc.start(static_cast<double>(startPosition) * (kPi / 180.0), static_cast<double>(launchAngle) * (kPi / 180.0));

        const core::Vec2 origin = osc.position();
        std::array<core::Vec2, kSteps> path {};
        std::array<float, kSteps> wave {};
        for (int i = 0; i < kSteps; ++i)
        {
            wave[static_cast<std::size_t>(i)] = osc.process(mixX, mixY);
            path[static_cast<std::size_t>(i)] = osc.position();
        }

        const auto& t = theme();
        const auto sx = [&](const double x) { return cx + radius * static_cast<float>(x); };
        const auto sy = [&](const double y) { return cy - radius * static_cast<float>(y); };

        // Polygon outline, same vertex rule as the oscillator.
        beginPath();
        sc(t.border);
        strokeWidth(1.5f);
        for (int i = 0; i <= sides; ++i)
        {
            const double theta = 2.0 * 3.14159265358979 * i / sides + 3.14159265358979 / sides;
            const float px = sx(std::cos(theta));
            const float py = sy(std::sin(theta));
            if (i == 0)
                moveTo(px, py);
            else
                lineTo(px, py);
        }
        stroke();

        // Path.
        beginPath();
        strokeColor(kOrbitAccent.r, kOrbitAccent.g, kOrbitAccent.b, 200);
        strokeWidth(1.0f);
        moveTo(sx(origin.x), sy(origin.y));
        for (const core::Vec2& p : path)
            lineTo(sx(p.x), sy(p.y));
        stroke();

        // Start point and launch direction.
        const float arrow = radius * 0.28f;
        const float launchRad = launchAngle * (kPi / 180.0f);
        beginPath();
        sc(t.textPrimary);
        strokeWidth(2.0f);
        moveTo(sx(origin.x), sy(origin.y));
        lineTo(sx(origin.x) + arrow * std::cos(launchRad), sy(origin.y) - arrow * std::sin(launchRad));
        stroke();
        beginPath();
        fc(t.textPrimary);
        circle(sx(origin.x), sy(origin.y), 4.0f);
        fill();

        // Scope strip: the waveform those samples make.
        const Rect scope {b.x + 14.0f, plot.y + plot.h + 8.0f, b.w - 28.0f, 48.0f};
        drawPlotFrame(scope);
        float peak = 0.05f;
        for (const float s : wave)
            peak = std::max(peak, std::fabs(s));
        beginPath();
        strokeColor(kOrbitAccent.r, kOrbitAccent.g, kOrbitAccent.b, 255);
        strokeWidth(1.2f);
        for (int i = 0; i < kSteps; ++i)
        {
            const float x = scope.x + 4.0f + (scope.w - 8.0f) * static_cast<float>(i) / (kSteps - 1);
            const float y = scope.y + scope.h * 0.5f - (scope.h * 0.5f - 6.0f) * wave[static_cast<std::size_t>(i)] / peak;
            if (i == 0)
                moveTo(x, y);
            else
                lineTo(x, y);
        }
        stroke();
        if (mixX <= 0.0f && mixY <= 0.0f)
            label(scope.x + scope.w * 0.5f, scope.y + scope.h * 0.5f, "X and Y Level are both zero: silent", true, 11.0f,
                  ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawEnvelopePanel(const Rect b)
    {
        drawPanel(b, "ENVELOPE", kEnvelopeAccent, "ADSR, one per voice");

        const Rect plot {b.x + 14.0f, b.y + 42.0f, b.w - 28.0f, 74.0f};
        drawEnvelopePlot(plot);

        // Two wide columns: the times are linear sliders, so width is resolution.
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - gap) / 2.0f;
        const float colA = b.x + 14.0f;
        const float colB = colA + w + gap;
        const float y = b.y + 126.0f;
        slider(core::ParamId::attack, "Attack", {colA, y, w, 40.0f}, kEnvelopeAccent);
        slider(core::ParamId::decay, "Decay", {colB, y, w, 40.0f}, kEnvelopeAccent);
        slider(core::ParamId::sustain, "Sustain", {colA, y + 44.0f, w, 40.0f}, kEnvelopeAccent);
        slider(core::ParamId::release, "Release", {colB, y + 44.0f, w, 40.0f}, kEnvelopeAccent);
    }

    void drawEnvelopePlot(const Rect plot)
    {
        drawPlotFrame(plot);

        const float attack = value(idx(core::ParamId::attack));
        const float decay = value(idx(core::ParamId::decay));
        const float sustain = value(idx(core::ParamId::sustain));
        const float release = value(idx(core::ParamId::release));

        // The decay really takes (1 - sustain) of its time, so draw it that way.
        const float aW = 0.08f + 0.20f * logPosition(core::ParamId::attack, attack);
        const float dW = (0.08f + 0.20f * logPosition(core::ParamId::decay, decay)) * std::max(0.15f, 1.0f - sustain);
        const float hW = 0.18f;
        const float rW = 0.08f + 0.20f * logPosition(core::ParamId::release, release);
        const float total = aW + dW + hW + rW;

        const float inner = plot.w - 16.0f;
        const float x0 = plot.x + 8.0f;
        const float bottom = plot.y + plot.h - 8.0f;
        const float height = plot.h - 16.0f;
        const auto px = [&](const float u) { return x0 + inner * u / total; };
        const auto py = [&](const float level) { return bottom - height * level; };

        beginPath();
        strokeColor(kEnvelopeAccent.r, kEnvelopeAccent.g, kEnvelopeAccent.b, 255);
        strokeWidth(2.0f);
        moveTo(px(0.0f), py(0.0f));
        lineTo(px(aW), py(1.0f));
        lineTo(px(aW + dW), py(sustain));
        lineTo(px(aW + dW + hW), py(sustain));
        lineTo(px(total), py(0.0f));
        stroke();

        const auto& t = theme();
        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(px(aW * 0.5f), plot.y + 3.0f, "A", nullptr);
        text(px(aW + dW * 0.5f), plot.y + 3.0f, "D", nullptr);
        text(px(aW + dW + hW * 0.5f), plot.y + 3.0f, "S", nullptr);
        text(px(aW + dW + hW + rW * 0.5f), plot.y + 3.0f, "R", nullptr);
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "VOICES  /  OUTPUT", kOutputAccent);

        const float x = b.x + 14.0f;
        const float w = b.w - 28.0f;
        slider(core::ParamId::velocity, "Velocity sensitivity", {x, b.y + 44.0f, w, 40.0f}, kOutputAccent);
        slider(core::ParamId::level, "Level  (CC 7)", {x, b.y + 88.0f, w, 40.0f}, kOutputAccent);

        const int channel = std::clamp(intValue(idx(core::ParamId::midiCh)), 0, 17);
        drawStepper(idx(core::ParamId::midiCh), "MIDI in channel",
                    core::kMidiChannelNames[static_cast<std::size_t>(channel)], {x, b.y + 132.0f, w, 40.0f});

        char voices[48];
        std::snprintf(voices, sizeof(voices), "Voices sounding: %d of %d", intValue(idx(core::ParamId::outVoices)),
                      static_cast<int>(core::kVoiceCount));
        label(x, b.y + 180.0f, voices, false, 12.0f);
        label(x, b.y + 197.0f, "Eight voices; notes set pitch, velocity sets level.", true, 10.5f);
    }
};

UI* createUI()
{
    return new TrajectoryUI();
}

END_NAMESPACE_DISTRHO
