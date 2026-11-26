#include "generative_panel_ui.hpp"
#include "downspout/look_and_feel.hpp"
#include "guardian_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

class GuardianUI : public GenerativePanelUI {
public:
    GuardianUI()
        : GenerativePanelUI(
            "Guardian",
            "Interpolate · Limit · Protect — bypass toggle top-right",
            downspout::guardian::kParameterSpecs.data(),
            downspout::guardian::kParameterCount, 230, 112, 94) {}

private:
    const laf::Theme& t_ { laf::defaultTheme() };
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    void onNanoDisplay() override
    {
        using namespace downspout::guardian;
        beginPanel();

        drawToggle(kBypass, 760, 22, 176, "Bypass", "Pass audio through all processing stages.");

        // SIGNAL CHAIN — Input, Attack, Ceiling, Release, Lookahead
        drawSection(24, 92, 912, 88, "SIGNAL CHAIN", "left to right signal path");
        drawSlider(kInputDb,    38, 124, 166, "Input", " dB",
                   "Pre-gain applied before limiting. Boost to drive harder; cut to reduce headroom consumption.", 1);
        drawSlider(kAttackMs,  220, 124, 166, "Attack", " ms",
                   "How quickly the limiter reduces gain when a peak is detected. 0 = instant.", 1);
        drawSlider(kCeilingDb, 402, 124, 166, "Ceiling", " dBFS",
                   "Maximum output level after interpolation, limiting and clipping.", 1);
        drawSlider(kReleaseMs, 584, 124, 166, "Release", " ms",
                   "Gain recovery time. Program release can shorten this for brief peaks.", 0);
        drawSlider(kLookaheadMs, 766, 124, 166, "Look-ahead", " ms",
                   "Delay used to anticipate peaks; this also changes plugin latency.", 1);

        // TRANSFER CURVE
        drawSection(24, 190, 912, 252, "TRANSFER CURVE", "input → output — ceiling and clipper shape");
        drawTransferCurve(38, 222, 880, 160);
        drawSlider(kClipperShape, 38, 386, 880, "Clipper shape", "",
                   "0 = limiter only. Drag right to add saturation; 1.0 = near-hard clip.", 2);

        // PROTECTION — input stage and peak guard
        drawSection(24, 454, 430, 186, "INPUT STAGE", "repair before the limiter");
        drawToggle(kInterpolate, 38, 482, 402, "Clip interpolation",
                   "Rebuild clipped input runs as a ramp between the last and next clean samples. Default on.");
        drawToggle(kTruePeak, 38, 530, 402, "4x true-peak guard",
                   "Detect inter-sample peaks with four-phase Catmull-Rom interpolation.");
        drawToggle(kDcRemove, 38, 578, 402, "DC removal",
                   "Remove very-low-frequency DC offset.");

        // DRIVE — automatic gain and adaptive release
        drawSection(468, 454, 468, 186, "DRIVE", "mix hot without squashing");
        drawToggle(kAutoDrive, 482, 482, 438, "Auto drive",
                   "Slowly trims input gain to hold the chosen peak reduction.");
        drawToggle(kProgramRelease, 482, 530, 438, "Program release",
                   "Recover faster after brief peaks, slower after sustained limiting.");
        drawSlider(kAutoTargetDb, 482, 578, 438, "Target reduction", " dB",
                   "Peak reduction that Auto drive aims for. Low values stay transparent; higher values are louder.", 1);

        // ACTIVITY — live protection density
        drawSection(24, 652, 912, 68, "ACTIVITY", "share of samples protected this block");
        drawMeter(kStatusDensity, 38, 676, 884, "Protection density", " %", 0);

        endPanel();
    }

    // Transfer function curve: X=input level (linear), Y=output level (linear)
    // Shows the limiter ceiling knee plus the clipper saturation curve.
    void drawTransferCurve(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::guardian;

        beginPath();
        fc(t_.background);
        roundedRect(x, y, w, h, 5);
        fill();

        const float minDb = -36.0f;
        const float maxDb =  6.0f;
        const float rangeDb = maxDb - minDb;

        fc(t_.border);
        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(x + w * 0.5f, y + 4.0f, "INPUT →", nullptr);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 4.0f, y + h * 0.5f, "OUT", nullptr);

        const float ceilingDb = value(kCeilingDb);
        auto dbToX = [&](const float db) { return x + (db - minDb) / rangeDb * w; };
        auto dbToY = [&](const float db) { return y + h - (db - minDb) / rangeDb * h; };

        beginPath();
        sc(t_.border);
        strokeWidth(1.0f);
        moveTo(dbToX(0.0f), y);
        lineTo(dbToX(0.0f), y + h);
        moveTo(x, dbToY(0.0f));
        lineTo(x + w, dbToY(0.0f));
        stroke();

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

        beginPath();
        sc(t_.border);
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
            float outLin = std::min(inLin, ceilingLin);
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
    }
};

UI* createUI() { return new GuardianUI(); }

END_NAMESPACE_DISTRHO
