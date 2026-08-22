#include "generative_panel_ui.hpp"
#include "guardian_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class GuardianUI : public GenerativePanelUI {
public:
    GuardianUI()
        : GenerativePanelUI(
            "Guardian",
            "Input · Limit · Clip · Output — bypass toggle top-right",
            downspout::guardian::kParameterSpecs.data(),
            downspout::guardian::kParameterCount, 230, 112, 94) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::guardian;
        beginPanel();

        // Bypass toggle in header area
        drawToggle(kBypass, 760, 22, 176, "Bypass", "Pass audio through all processing stages.");

        // SIGNAL CHAIN section — Input, Attack, Ceiling, Release, Lookahead
        drawSection(24, 104, 912, 92, "SIGNAL CHAIN", "left to right signal path");
        drawSlider(kInputDb,    38, 137, 166, "Input", " dB",
                   "Pre-gain applied before limiting. Boost to drive harder; cut to reduce headroom consumption.", 1);
        drawSlider(kAttackMs,  220, 137, 166, "Attack", " ms",
                   "How quickly the limiter reduces gain when a peak is detected. 0 = instant.", 1);
        drawSlider(kCeilingDb, 402, 137, 166, "Ceiling", " dBFS",
                   "Maximum output level after limiting and clipping.", 1);
        drawSlider(kReleaseMs, 584, 137, 166, "Release", " ms",
                   "How quickly gain returns after limiting.", 0);
        drawSlider(kLookaheadMs, 766, 137, 166, "Look-ahead", " ms",
                   "Delay used to anticipate peaks; this also changes plugin latency.", 1);

        // TRANSFER CURVE section
        drawSection(24, 212, 912, 258, "TRANSFER CURVE", "input → output — ceiling and clipper shape");
        drawTransferCurve(38, 244, 880, 212);

        // Clipper shape slider inside the curve section
        drawSlider(kClipperShape, 38, 458, 880, "Clipper shape",
                   "",
                   "0 = no clipping (limiter only). Drag right to add saturation; 1.0 = near-hard clip.", 2);

        // PROTECTION section (left)
        drawSection(24, 530, 430, 168, "PROTECTION", "independent safety stages");
        drawToggle(kDcRemove,  38, 563, 402, "DC removal",      "Remove very-low-frequency DC offset.");
        drawToggle(kTruePeak,  38, 613, 402, "True-peak guard", "Use inter-sample estimates to protect output peaks.");
        drawSlider(kSilenceDb, 38, 660, 402, "Silence threshold", " dBFS",
                   "Level below which the signal is considered silent.", 0);

        // SAFETY MONITOR section (right)
        drawSection(468, 530, 468, 168, "SAFETY MONITOR", "read-only processor diagnostics");
        drawGainReductionBar(482, 563, 440, 36);
        drawTruePeakBar(482, 609, 440, 36);
        drawLamp(482, 657, 212, "Overload latched", value(kStatusOverload) >= 0.5f, true);
        drawNeutralLamp(702, 657, 212, "Silence detected", value(kStatusSilence) >= 0.5f);

        // Reset + info row
        drawAction(1, 38, 712, 402, 42, "Reset latched diagnostics",
                   "Clear the fault counter, overload latch, and signal history.");
        char faults[32] {};
        std::snprintf(faults, sizeof(faults), "%d", static_cast<int>(std::lround(value(kStatusFaults))));
        drawReadout(482, 712, 204, 42, "Recovered faults", faults);
        char latency[40] {};
        std::snprintf(latency, sizeof(latency), "%.1f ms", value(kLookaheadMs));
        drawReadout(698, 712, 212, 42, "Reported look-ahead", latency);

        endPanel();
    }

    void actionTriggered(const int action) override
    {
        using namespace downspout::guardian;
        if (action == 1)
            commitParameter(kReset, value(kReset) + 1.0f);
    }

    // Transfer function curve: X=input level (linear), Y=output level (linear)
    // Shows limiter ceiling knee plus clipper saturation curve.
    void drawTransferCurve(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::guardian;

        // Background
        beginPath();
        fillColor(13, 17, 21, 255);
        roundedRect(x, y, w, h, 5);
        fill();

        // Axis labels
        const float minDb = -36.0f;
        const float maxDb =  6.0f;
        const float rangeDb = maxDb - minDb;

        fillColor(80, 90, 98, 255);
        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(x + w * 0.5f, y + 4.0f, "INPUT →", nullptr);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 4.0f, y + h * 0.5f, "OUT", nullptr);

        // Grid lines at 0 dB input and at ceiling
        const float ceilingDb = value(kCeilingDb);
        auto dbToX = [&](const float db) { return x + (db - minDb) / rangeDb * w; };
        auto dbToY = [&](const float db) { return y + h - (db - minDb) / rangeDb * h; };

        // 0 dB reference lines
        beginPath();
        strokeColor(45, 55, 63, 255);
        strokeWidth(1.0f);
        moveTo(dbToX(0.0f), y);
        lineTo(dbToX(0.0f), y + h);
        moveTo(x, dbToY(0.0f));
        lineTo(x + w, dbToY(0.0f));
        stroke();

        // Ceiling line
        beginPath();
        strokeColor(160, 90, 60, 180);
        strokeWidth(1.0f);
        moveTo(x, dbToY(ceilingDb));
        lineTo(x + w, dbToY(ceilingDb));
        stroke();
        fillColor(160, 90, 60, 200);
        fontSize(9.0f);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        text(x + w - 4.0f, dbToY(ceilingDb) - 2.0f, "CEILING", nullptr);

        // Unity gain diagonal (grey dashed look — draw as series of short segments)
        beginPath();
        strokeColor(55, 65, 73, 255);
        strokeWidth(1.0f);
        const int steps = 40;
        for (int i = 0; i < steps; ++i) {
            const float t0 = static_cast<float>(i) / steps;
            const float t1 = (static_cast<float>(i) + 0.5f) / steps;
            const float d0 = minDb + t0 * rangeDb;
            const float d1 = minDb + t1 * rangeDb;
            moveTo(dbToX(d0), dbToY(d0));
            lineTo(dbToX(d1), dbToY(d1));
        }
        stroke();

        // Transfer curve: compute per pixel
        const float inputGainDb = value(kInputDb);
        const float shape = value(kClipperShape);
        const float ceilingLin = std::pow(10.0f, ceilingDb / 20.0f);
        const float clipThreshold = ceilingLin * (1.0f - shape);
        const float clipRange = ceilingLin - clipThreshold;
        const float clipK = 1.0f + shape * 9.0f;
        const float clipTanhK = shape > 0.001f ? std::tanh(clipK) : 1.0f;
        auto softClip = [&](float v) -> float {
            const float absV = std::fabs(v);
            if (absV <= clipThreshold) return v;
            if (clipRange < 1e-6f) return std::copysign(ceilingLin, v);
            const float t = std::min((absV - clipThreshold) / clipRange, 1.0f);
            return std::copysign(clipThreshold + clipRange * std::tanh(t * clipK) / clipTanhK, v);
        };

        beginPath();
        bool first = true;
        const int pixels = static_cast<int>(w);
        for (int px = 0; px < pixels; ++px) {
            const float inDb = minDb + static_cast<float>(px) / static_cast<float>(pixels) * rangeDb;
            float inLin = std::pow(10.0f, (inDb + inputGainDb) / 20.0f);
            // Limiter: clamp to ceiling (instantaneous model for curve display)
            float outLin = std::min(inLin, ceilingLin);
            // Clipper (knee-based soft clip)
            if (shape > 0.001f)
                outLin = softClip(outLin);
            outLin = std::min(outLin, ceilingLin);
            const float outDb = outLin > 1e-9f ? 20.0f * std::log10(outLin) : minDb - 6.0f;
            const float cx = x + static_cast<float>(px);
            const float cy = dbToY(std::clamp(outDb, minDb - 1.0f, maxDb + 1.0f));
            if (first) { moveTo(cx, cy); first = false; }
            else lineTo(cx, cy);
        }
        strokeColor(accentR(), accentG(), accentB(), 230);
        strokeWidth(2.0f);
        stroke();

        // Current peak indicator — vertical line at current input peak level
        const float peakLin = value(kStatusPeak);
        if (peakLin > 1e-9f) {
            const float peakDb = std::clamp(20.0f * std::log10(peakLin) - inputGainDb,
                                            minDb, maxDb);
            beginPath();
            strokeColor(220, 200, 80, 160);
            strokeWidth(1.5f);
            moveTo(dbToX(peakDb), y);
            lineTo(dbToX(peakDb), y + h);
            stroke();
        }
    }

    void drawGainReductionBar(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::guardian;
        beginPath();
        fillColor(18, 23, 28, 255);
        roundedRect(x, y, w, h, 4);
        fill();
        fillColor(131, 143, 151, 255);
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 8.0f, y + h * 0.5f, "GR", nullptr);
        const float reduction = std::clamp(value(kStatusReduction), 0.0f, 48.0f);
        const float tw = w - 60.0f;
        beginPath();
        fillColor(30, 37, 44, 255);
        roundedRect(x + 28.0f, y + 6.0f, tw, h - 12.0f, 3);
        fill();
        beginPath();
        fillColor(accentR(), accentG(), accentB(), 220);
        roundedRect(x + 28.0f, y + 6.0f, std::max(3.0f, tw * reduction / 48.0f), h - 12.0f, 3);
        fill();
        char buf[24] {};
        std::snprintf(buf, sizeof(buf), "%.1f dB", reduction);
        fillColor(200, 210, 205, 255);
        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(x + w - 4.0f, y + h * 0.5f, buf, nullptr);
    }

    void drawTruePeakBar(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::guardian;
        beginPath();
        fillColor(18, 23, 28, 255);
        roundedRect(x, y, w, h, 4);
        fill();
        fillColor(131, 143, 151, 255);
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 8.0f, y + h * 0.5f, "TP", nullptr);
        const float peak = std::max(value(kStatusPeak), 0.000001f);
        const float db = std::clamp(20.0f * std::log10(peak), -120.0f, 6.0f);
        const float normal = (db + 120.0f) / 126.0f;
        const float tw = w - 60.0f;
        beginPath();
        fillColor(30, 37, 44, 255);
        roundedRect(x + 28.0f, y + 6.0f, tw, h - 12.0f, 3);
        fill();
        beginPath();
        fillColor(db > -0.1f ? 238 : accentR(), db > -0.1f ? 79 : accentG(),
                  db > -0.1f ? 68 : accentB(), 220);
        roundedRect(x + 28.0f, y + 6.0f, std::max(3.0f, tw * normal), h - 12.0f, 3);
        fill();
        char buf[28] {};
        std::snprintf(buf, sizeof(buf), "%.1f dBFS", db);
        fillColor(200, 210, 205, 255);
        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(x + w - 4.0f, y + h * 0.5f, buf, nullptr);
    }

    void drawNeutralLamp(const float x, const float y, const float w, const char* label, const bool on)
    {
        beginPath();
        fillColor(25, 31, 37, 255);
        roundedRect(x, y, w, 37, 5);
        fill();
        beginPath();
        fillColor(on ? 104 : 50, on ? 166 : 57, on ? 194 : 63, 255);
        circle(x + 16, y + 18.5f, 6);
        fill();
        fillColor(on ? 206 : 130, on ? 220 : 140, on ? 225 : 148, 255);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 29, y + 18.5f, label, nullptr);
    }
};

UI* createUI() { return new GuardianUI(); }

END_NAMESPACE_DISTRHO
