#include "generative_panel_ui.hpp"
#include "loopdelay_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

class LoopdelayUI : public GenerativePanelUI {
public:
    LoopdelayUI()
        : GenerativePanelUI(
            "Loopdelay",
            "Stereo echo + capture loop · producer-ready CC 30 time / CC 31 feedback",
            downspout::loopdelay::kParameterSpecs.data(),
            downspout::loopdelay::kParameterCount, 111, 204, 167) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::loopdelay;
        beginPanel();
        const bool loop = value(kMode) >= 0.5f;
        const bool sync = value(kTimeMode) >= 0.5f;

        drawSection(24, 102, 1032, 66, "SIGNAL FLOW", "built to follow the eight-channel mixer");
        drawReadout(38, 120, 1004, 40, "RECOMMENDED CHAIN", "T-Mix  →  Loopdelay  →  Guardian");

        drawSection(24, 174, 612, 230, "ENGINE", "choose echo or a continuously overdubbable loop");
        static constexpr const char* modes[] {"Delay", "Loop"};
        static constexpr const char* clocks[] {"Free", "Sync"};
        static constexpr const char* lengths[] {
            "¼ beat", "½ beat", "1 beat", "2 beats", "1 bar", "2 bars", "4 bars"
        };
        drawChoice(kMode, 38, 208, 282, "What should it do?", modes, 2,
                   "Delay echoes continuously; Loop captures one selected-length pass and repeats it.");
        drawChoice(kTimeMode, 334, 208, 288, "Time source", clocks, 2,
                   "Free uses milliseconds; Sync derives length from the host BBT transport.");
        drawSlider(kFreeTimeMs, 38, 270, 282, "Free time", " ms",
                   "Delay or loop length when Time source is Free.", 0, !sync);
        drawChoice(kSyncLength, 334, 270, 288, "Musical length", lengths, 7,
                   "A beat follows the host time-signature denominator; a bar follows its full meter.", sync);
        drawPercentSlider(kOverdub, 38, 332, 282, "Loop overdub",
                          "How much live input is written into each loop pass.", loop);
        if (loop)
            drawAction(0, 334, 337, 288, 38, "CLEAR + CAPTURE NEXT PASS",
                       "Discard the current loop and capture a fresh pass.", true);
        else
            drawReadout(334, 337, 288, 38, "LOOP CAPTURE", "available in Loop mode");

        drawSection(652, 174, 404, 230, "SOUND", "shape the repeat and the return");
        drawPercentSlider(kFeedback, 666, 208, 180, "Feedback",
                          "Delay regeneration, or how much loop memory survives each pass.");
        drawPercentSlider(kMix, 860, 208, 182, "Dry / wet",
                          "0% is transparent dry audio; 100% is effect only.");
        drawPercentSlider(kTone, 666, 270, 180, "Repeat tone",
                          "Darkens or opens the repeated signal.");
        drawPercentSlider(kPingPong, 860, 270, 182, "Ping-pong",
                          "Cross-feeds delay returns between left and right.", !loop);
        drawSlider(kOutputDb, 666, 332, 376, "Output trim", " dB",
                   "Match the processed signal to the surrounding chain.", 1);

        drawSection(24, 420, 612, 164, "MIDI PRODUCER", "route Mixgen-style controller MIDI to this track");
        drawToggle(kMidiEnabled, 38, 454, 188, "Accept MIDI control",
                   "Listen for the fixed Loopdelay MIDI contract on every channel.");
        drawReadout(240, 451, 180, 46, "CC 30 · TIME", sync ? "selects musical length" : "20–4000 ms curve");
        drawReadout(434, 451, 188, 46, "CC 31 · FEEDBACK", "0–100% regeneration");
        drawLamp(38, 514, 188, "TIME TAKEN OVER", value(kStatusTimeMidi) >= 0.5f);
        drawLamp(240, 514, 180, "FEEDBACK TAKEN", value(kStatusFeedbackMidi) >= 0.5f);
        drawAction(1, 434, 514, 188, 37, "RELEASE MIDI",
                   "Return time and feedback to the saved panel values.");

        drawSection(652, 420, 404, 240, "LIVE", "effective values from the audio engine");
        char stateText[24] {};
        const int stateIndex = std::clamp(static_cast<int>(std::lround(value(kStatusState))), 0, 2);
        std::snprintf(stateText, sizeof(stateText), "%s", kStateNames[stateIndex]);
        char timeText[32] {};
        std::snprintf(timeText, sizeof(timeText), value(kStatusDelayMs) >= 1000.0f ? "%.2f s" : "%.0f ms",
                      value(kStatusDelayMs) >= 1000.0f ? value(kStatusDelayMs) / 1000.0f : value(kStatusDelayMs));
        char feedbackText[24] {};
        std::snprintf(feedbackText, sizeof(feedbackText), "%d%%",
                      static_cast<int>(std::lround(value(kStatusFeedback) * 100.0f)));
        drawReadout(666, 454, 112, 50, "ENGINE", stateText);
        drawReadout(790, 454, 120, 50, "TIME", timeText);
        drawReadout(922, 454, 120, 50, "FEEDBACK", feedbackText);
        drawMeter(kStatusLoopProgress, 666, 520, 376, "Loop position");
        drawMeter(kStatusInputPeak, 666, 574, 180, "Input level");
        drawMeter(kStatusOutputPeak, 860, 574, 182, "Output level");
        endPanel();
    }

    void actionTriggered(const int action) override
    {
        using namespace downspout::loopdelay;
        const std::uint32_t parameter = action == 0 ? kClear : kResetMidi;
        commitParameter(parameter, 1.0f);
        commitParameter(parameter, 0.0f);
    }
};

UI* createUI() { return new LoopdelayUI(); }
END_NAMESPACE_DISTRHO
