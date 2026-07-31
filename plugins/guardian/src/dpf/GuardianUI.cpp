#include "generative_panel_ui.hpp"
#include "guardian_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class GuardianUI : public GenerativePanelUI {
public:
    GuardianUI()
        : GenerativePanelUI(
            "Guardian",
            "Set the ceiling first; protection, gain reduction, and latched faults stay visible",
            downspout::guardian::kParameterSpecs.data(),
            downspout::guardian::kParameterCount, 230, 112, 94) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::guardian;
        beginPanel();

        drawSection(24, 104, 430, 244, "LIMITER", "the selected look-ahead is reported to the host");
        drawSlider(kCeilingDb, 38, 137, 402, "Output ceiling", " dBFS",
                   "Maximum output level after limiting.", 1);
        drawSlider(kLookaheadMs, 38, 193, 194, "Look-ahead", " ms",
                   "Delay used to anticipate peaks; this also changes plugin latency.", 1);
        drawSlider(kReleaseMs, 240, 193, 200, "Release", " ms",
                   "How quickly gain returns after limiting.", 0);
        drawSlider(kSilenceDb, 38, 249, 402, "Silence threshold", " dBFS",
                   "Level below which the signal is considered silent.", 0);

        drawSection(24, 360, 430, 271, "PROTECTION", "independent safety stages");
        drawToggle(kDcRemove, 38, 393, 402, "DC removal", "Remove very-low-frequency DC offset.");
        drawToggle(kTruePeak, 38, 443, 402, "True-peak guard", "Use inter-sample estimates to protect output peaks.");
        drawToggle(kSoftClip, 38, 493, 402, "Soft clipping", "Round exceptional peaks instead of hard clipping.");
        drawAction(1, 38, 550, 402, 42, "Reset latched diagnostics",
                   "Clear the fault counter, overload latch, and signal history.");

        drawSection(468, 104, 468, 527, "SAFETY MONITOR", "read-only processor diagnostics");
        drawGainReduction(492, 139, 420, 129);
        drawTruePeak(492, 282, 420, 129);
        drawMeter(kStatusReduction, 492, 427, 204, "Gain reduction", " dB", 1);
        char peakText[32] {};
        const float peak = std::max(value(kStatusPeak), 0.000001f);
        std::snprintf(peakText, sizeof(peakText), "%.1f dBFS", 20.0f * std::log10(peak));
        drawReadout(708, 427, 204, 42, "True peak", peakText);
        drawLamp(492, 483, 204, "Overload latched", value(kStatusOverload) >= 0.5f, true);
        drawNeutralLamp(708, 483, 204, "Silence detected", value(kStatusSilence) >= 0.5f);
        char faults[32] {};
        std::snprintf(faults, sizeof(faults), "%d", static_cast<int>(std::lround(value(kStatusFaults))));
        drawReadout(492, 534, 204, 58, "Recovered faults", faults);
        char latency[40] {};
        std::snprintf(latency, sizeof(latency), "%.1f ms", value(kLookaheadMs));
        drawReadout(708, 534, 204, 58, "Reported look-ahead", latency);

        endPanel();
    }

    void actionTriggered(const int action) override
    {
        using namespace downspout::guardian;
        if (action == 1)
            commitParameter(kReset, value(kReset) + 1.0f);
    }

    void drawGainReduction(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::guardian;
        beginPath();
        fillColor(18, 23, 28, 255);
        roundedRect(x, y, w, h, 6);
        fill();
        fillColor(131, 143, 151, 255);
        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 12, y + 10, "GAIN REDUCTION", nullptr);
        const float reduction = std::clamp(value(kStatusReduction), 0.0f, 48.0f);
        const float normal = reduction / 48.0f;
        beginPath();
        fillColor(52, 59, 65, 255);
        roundedRect(x + 12, y + 42, w - 24, 36, 5);
        fill();
        beginPath();
        fillColor(accentR(), accentG(), accentB(), 235);
        roundedRect(x + 12, y + 42, std::max(3.0f, (w - 24) * normal), 36, 5);
        fill();
        char valueText[32] {};
        std::snprintf(valueText, sizeof(valueText), "%.1f dB", reduction);
        fillColor(236, 239, 236, 255);
        fontSize(18);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        text(x + w - 12, y + h - 10, valueText, nullptr);
    }

    void drawTruePeak(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::guardian;
        beginPath();
        fillColor(18, 23, 28, 255);
        roundedRect(x, y, w, h, 6);
        fill();
        fillColor(131, 143, 151, 255);
        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 12, y + 10, "TRUE PEAK", nullptr);
        const float peak = std::max(value(kStatusPeak), 0.000001f);
        const float db = std::clamp(20.0f * std::log10(peak), -120.0f, 6.0f);
        const float normal = (db + 120.0f) / 126.0f;
        beginPath();
        fillColor(52, 59, 65, 255);
        roundedRect(x + 12, y + 42, w - 24, 36, 5);
        fill();
        beginPath();
        fillColor(db > -0.1f ? 238 : accentR(), db > -0.1f ? 79 : accentG(),
                  db > -0.1f ? 68 : accentB(), 235);
        roundedRect(x + 12, y + 42, std::max(3.0f, (w - 24) * normal), 36, 5);
        fill();
        char valueText[32] {};
        std::snprintf(valueText, sizeof(valueText), "%.1f dBFS", db);
        fillColor(236, 239, 236, 255);
        fontSize(18);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        text(x + w - 12, y + h - 10, valueText, nullptr);
    }

    void drawNeutralLamp(const float x,
                         const float y,
                         const float w,
                         const char* label,
                         const bool on)
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
