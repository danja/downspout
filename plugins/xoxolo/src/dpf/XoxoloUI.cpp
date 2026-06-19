#include "DistrhoUI.hpp"

#include "xoxolo_engine.hpp"
#include "xoxolo_params.hpp"
#include "xoxolo_serialization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(const float px, const float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

[[nodiscard]] int clampi(const int value, const int minimum, const int maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] int stepsPerBeatForResolution(const downspout::xoxolo::ResolutionId resolution)
{
    switch (resolution) {
    case downspout::xoxolo::ResolutionId::quarter: return 1;
    case downspout::xoxolo::ResolutionId::eighth: return 2;
    case downspout::xoxolo::ResolutionId::sixteenth: return 4;
    case downspout::xoxolo::ResolutionId::count: break;
    }
    return 4;
}

[[nodiscard]] bool supportedCombination(const int bars, const downspout::xoxolo::ResolutionId resolution)
{
    return bars >= 1 && bars <= 4 && bars * 4 * stepsPerBeatForResolution(resolution) <= downspout::xoxolo::kMaxSteps;
}

[[nodiscard]] const char* resolutionName(const downspout::xoxolo::ResolutionId resolution)
{
    switch (resolution) {
    case downspout::xoxolo::ResolutionId::quarter: return "1/4";
    case downspout::xoxolo::ResolutionId::eighth: return "1/8";
    case downspout::xoxolo::ResolutionId::sixteenth: return "1/16";
    case downspout::xoxolo::ResolutionId::count: break;
    }
    return "1/16";
}

}  // namespace

class XoxoloUI : public UI
{
public:
    XoxoloUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        pattern_ = downspout::xoxolo::makeDefaultPattern();
        values_.fill(0.0f);
        values_[downspout::xoxolo::kParamBars] = 1.0f;
        values_[downspout::xoxolo::kParamResolution] = 2.0f;
        values_[downspout::xoxolo::kParamChannel] = 10.0f;
        values_[downspout::xoxolo::kParamCurrentStep] = -1.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= values_.size())
            return;
        values_[index] = value;
        if (index == downspout::xoxolo::kParamBars ||
            index == downspout::xoxolo::kParamResolution ||
            index == downspout::xoxolo::kParamChannel) {
            applyParameterControlsToPattern(false);
        }
        repaint();
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (std::string(key != nullptr ? key : "") != downspout::xoxolo::kStateKeyPattern)
            return;
        const auto pattern = downspout::xoxolo::deserializePatternState(value != nullptr ? value : "");
        if (!pattern.has_value())
            return;
        pattern_ = *pattern;
        values_[downspout::xoxolo::kParamBars] = static_cast<float>(pattern_.bars);
        values_[downspout::xoxolo::kParamResolution] = static_cast<float>(static_cast<int>(pattern_.resolution));
        values_[downspout::xoxolo::kParamChannel] = static_cast<float>(pattern_.channel);
        repaint();
    }

    void uiIdle() override
    {
        if (pulseFrames_ > 0) {
            --pulseFrames_;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        drawBackground(width, height);
        drawHeader(20.0f, 18.0f, width - 40.0f, 58.0f);

        const float controlsW = 154.0f;
        const float pad = 20.0f;
        const float gridX = pad;
        const float gridY = 98.0f;
        const float gridW = width - controlsW - pad * 3.0f;
        const float gridH = height - gridY - pad;

        drawGrid(gridX, gridY, gridW, gridH);
        drawControls(width - controlsW - pad, gridY, controlsW, gridH);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press)
            return false;

        for (int lane = 0; lane < downspout::xoxolo::kLaneCount; ++lane) {
            if (previewRects_[static_cast<std::size_t>(lane)].contains(x, y)) {
                triggerPreview(lane);
                return true;
            }
            if (noteRects_[static_cast<std::size_t>(lane)].contains(x, y)) {
                nudgeLaneNote(lane, 1);
                return true;
            }
        }

        for (int lane = 0; lane < downspout::xoxolo::kLaneCount; ++lane) {
            for (int step = 0; step < pattern_.totalSteps; ++step) {
                if (cellRects_[static_cast<std::size_t>(lane)][static_cast<std::size_t>(step)].contains(x, y)) {
                    const bool active = !downspout::xoxolo::cellActive(pattern_, lane, step);
                    downspout::xoxolo::setCell(pattern_, lane, step, active);
                    pushPatternState();
                    repaint();
                    return true;
                }
            }
        }

        if (barsRect_.contains(x, y)) {
            cycleBars();
            return true;
        }
        if (resolutionRect_.contains(x, y)) {
            cycleResolution();
            return true;
        }
        if (channelRect_.contains(x, y)) {
            cycleChannel();
            return true;
        }
        if (clearRect_.contains(x, y)) {
            downspout::xoxolo::clearPattern(pattern_);
            pushPatternState();
            triggerParameter(downspout::xoxolo::kParamClear);
            pulseFrames_ = 8;
            repaint();
            return true;
        }

        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        const int direction = ev.delta.getY() > 0.0f ? 1 : -1;

        for (int lane = 0; lane < downspout::xoxolo::kLaneCount; ++lane) {
            if (noteRects_[static_cast<std::size_t>(lane)].contains(x, y)) {
                nudgeLaneNote(lane, direction);
                return true;
            }
        }
        if (barsRect_.contains(x, y)) {
            cycleBars(direction);
            return true;
        }
        if (resolutionRect_.contains(x, y)) {
            cycleResolution(direction);
            return true;
        }
        if (channelRect_.contains(x, y)) {
            cycleChannel(direction);
            return true;
        }
        return false;
    }

private:
    std::array<float, downspout::xoxolo::kParameterCount> values_ {};
    downspout::xoxolo::PatternState pattern_ {};
    std::array<std::array<Rect, downspout::xoxolo::kMaxSteps>, downspout::xoxolo::kLaneCount> cellRects_ {};
    std::array<Rect, downspout::xoxolo::kLaneCount> noteRects_ {};
    std::array<Rect, downspout::xoxolo::kLaneCount> previewRects_ {};
    Rect barsRect_ {};
    Rect resolutionRect_ {};
    Rect channelRect_ {};
    Rect clearRect_ {};
    int pulseFrames_ = 0;

    void drawBackground(const float width, const float height)
    {
        beginPath();
        rect(0.0f, 0.0f, width, height);
        fillColor(14, 16, 18, 255);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(27, 31, 35, 255);
        fill();
        closePath();

        fontSize(25.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(238, 241, 242, 255);
        text(x + 16.0f, y + h * 0.5f - 3.0f, "Xoxolo", nullptr);

        fontSize(12.0f);
        fillColor(156, 168, 174, 255);
        text(x + 122.0f, y + h * 0.5f - 2.0f, "simple MIDI drum pattern editor", nullptr);

        char stepsText[48];
        std::snprintf(stepsText, sizeof(stepsText), "%d steps", pattern_.totalSteps);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(202, 211, 216, 255);
        text(x + w - 16.0f, y + h * 0.5f - 2.0f, stepsText, nullptr);
    }

    void drawGrid(const float x, const float y, const float w, const float h)
    {
        const float labelW = 82.0f;
        const float noteW = 44.0f;
        const float previewW = 28.0f;
        const float gap = 6.0f;
        const float rowGap = 5.0f;
        const float rowH = (h - rowGap * static_cast<float>(downspout::xoxolo::kLaneCount - 1)) /
                           static_cast<float>(downspout::xoxolo::kLaneCount);
        const float gridX = x + labelW;
        const float rightW = noteW + previewW + gap * 2.0f;
        const float stepGap = 3.0f;
        const float cellW = (w - labelW - rightW - stepGap * static_cast<float>(pattern_.totalSteps - 1)) /
                            static_cast<float>(pattern_.totalSteps);
        const int currentStep = clampi(static_cast<int>(std::lround(values_[downspout::xoxolo::kParamCurrentStep])),
                                       -1,
                                       downspout::xoxolo::kMaxSteps - 1);

        for (int lane = 0; lane < downspout::xoxolo::kLaneCount; ++lane) {
            const float rowY = y + static_cast<float>(lane) * (rowH + rowGap);
            drawLaneLabel(x, rowY, labelW - 8.0f, rowH, lane);

            for (int step = 0; step < pattern_.totalSteps; ++step) {
                const float cellX = gridX + static_cast<float>(step) * (cellW + stepGap);
                const Rect rect {cellX, rowY, cellW, rowH};
                cellRects_[static_cast<std::size_t>(lane)][static_cast<std::size_t>(step)] = rect;
                drawCell(rect, downspout::xoxolo::cellActive(pattern_, lane, step), step == currentStep, step);
            }

            for (int step = pattern_.totalSteps; step < downspout::xoxolo::kMaxSteps; ++step)
                cellRects_[static_cast<std::size_t>(lane)][static_cast<std::size_t>(step)] = {};

            const float noteX = gridX + static_cast<float>(pattern_.totalSteps) * (cellW + stepGap) + gap;
            noteRects_[static_cast<std::size_t>(lane)] = {noteX, rowY, noteW, rowH};
            previewRects_[static_cast<std::size_t>(lane)] = {noteX + noteW + gap, rowY, previewW, rowH};
            drawNoteBox(noteRects_[static_cast<std::size_t>(lane)], pattern_.lanes[static_cast<std::size_t>(lane)].midiNote);
            drawPreview(previewRects_[static_cast<std::size_t>(lane)]);
        }
    }

    void drawLaneLabel(const float x, const float y, const float w, const float h, const int lane)
    {
        beginPath();
        roundedRect(x, y, w, h, 6.0f);
        fillColor(26, 30, 34, 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(207, 216, 220, 255);
        text(x + 9.0f, y + h * 0.5f, downspout::xoxolo::kDefaultLanes[static_cast<std::size_t>(lane)].name, nullptr);
    }

    void drawCell(const Rect& rect, const bool active, const bool current, const int step)
    {
        int r = active ? 232 : 35;
        int g = active ? 117 : 41;
        int b = active ? 75 : 47;
        if (!active && (step % 4) == 0) {
            r = 45;
            g = 52;
            b = 58;
        }

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 5.0f);
        fillColor(r, g, b, 255);
        fill();
        closePath();

        if (current) {
            beginPath();
            roundedRect(rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f, 4.0f);
            strokeColor(244, 221, 132, 240);
            strokeWidth(2.0f);
            stroke();
            closePath();
        }
    }

    void drawNoteBox(const Rect& rect, const int note)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 6.0f);
        fillColor(31, 38, 43, 255);
        fill();
        closePath();

        char textValue[8];
        std::snprintf(textValue, sizeof(textValue), "%d", note);
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(224, 231, 234, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, textValue, nullptr);
    }

    void drawPreview(const Rect& rect)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 6.0f);
        fillColor(54, 65, 75, 255);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(235, 240, 242, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, ">", nullptr);
    }

    void drawControls(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(24, 29, 33, 255);
        fill();
        closePath();

        barsRect_ = {x + 14.0f, y + 42.0f, w - 28.0f, 44.0f};
        resolutionRect_ = {x + 14.0f, y + 102.0f, w - 28.0f, 44.0f};
        channelRect_ = {x + 14.0f, y + 162.0f, w - 28.0f, 44.0f};
        clearRect_ = {x + 14.0f, y + h - 58.0f, w - 28.0f, 42.0f};

        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(226, 232, 235, 255);
        text(x + 14.0f, y + 16.0f, "Pattern", nullptr);

        char barsValue[12];
        std::snprintf(barsValue, sizeof(barsValue), "%d", pattern_.bars);
        drawSelector(barsRect_, "Bars", barsValue);
        drawSelector(resolutionRect_, "Resolution", resolutionName(pattern_.resolution));

        char channelValue[12];
        std::snprintf(channelValue, sizeof(channelValue), "%d", pattern_.channel);
        drawSelector(channelRect_, "Channel", channelValue);
        drawButton(clearRect_, "Clear");
    }

    void drawSelector(const Rect& rect, const char* label, const char* value)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(35, 42, 48, 255);
        fill();
        closePath();

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(146, 158, 164, 255);
        text(rect.x + 10.0f, rect.y + 7.0f, label, nullptr);
        fontSize(14.0f);
        fillColor(229, 235, 238, 255);
        text(rect.x + 10.0f, rect.y + 25.0f, value, nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(131, 145, 153, 255);
        text(rect.x + rect.w - 10.0f, rect.y + rect.h * 0.5f, ">", nullptr);
    }

    void drawButton(const Rect& rect, const char* label)
    {
        const int pulse = pulseFrames_ > 0 ? 18 : 0;
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(111 + pulse, 73 + pulse, 67 + pulse, 255);
        fill();
        closePath();

        fontSize(14.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(245, 239, 236, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, label, nullptr);
    }

    void applyParameterControlsToPattern(const bool pushState)
    {
        const int bars = clampi(static_cast<int>(std::lround(values_[downspout::xoxolo::kParamBars])), 1, 4);
        const auto resolution = static_cast<downspout::xoxolo::ResolutionId>(
            clampi(static_cast<int>(std::lround(values_[downspout::xoxolo::kParamResolution])), 0, 2));
        const int channel = clampi(static_cast<int>(std::lround(values_[downspout::xoxolo::kParamChannel])), 1, 16);
        pattern_.channel = channel;
        downspout::xoxolo::resizePattern(pattern_, bars, resolution, downspout::meterFromTimeSignature(4.0, 4.0));
        if (pushState)
            pushPatternState();
    }

    void pushPatternState()
    {
        setState(downspout::xoxolo::kStateKeyPattern, downspout::xoxolo::serializePatternState(pattern_).c_str());
    }

    void setParameter(const uint32_t index, const float value)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
    }

    void triggerParameter(const uint32_t index)
    {
        const float next = values_[index] + 1.0f;
        setParameter(index, next);
    }

    void cycleBars(const int direction = 1)
    {
        int next = pattern_.bars;
        for (int attempt = 0; attempt < 4; ++attempt) {
            next += direction;
            if (next > 4)
                next = 1;
            if (next < 1)
                next = 4;
            if (supportedCombination(next, pattern_.resolution))
                break;
        }
        pattern_.bars = next;
        downspout::xoxolo::resizePattern(pattern_, pattern_.bars, pattern_.resolution, downspout::meterFromTimeSignature(4.0, 4.0));
        setParameter(downspout::xoxolo::kParamBars, static_cast<float>(pattern_.bars));
        pushPatternState();
        repaint();
    }

    void cycleResolution(const int direction = 1)
    {
        int next = static_cast<int>(pattern_.resolution);
        for (int attempt = 0; attempt < 3; ++attempt) {
            next = (next + direction + 3) % 3;
            const auto candidate = static_cast<downspout::xoxolo::ResolutionId>(next);
            if (supportedCombination(pattern_.bars, candidate)) {
                pattern_.resolution = candidate;
                break;
            }
        }
        downspout::xoxolo::resizePattern(pattern_, pattern_.bars, pattern_.resolution, downspout::meterFromTimeSignature(4.0, 4.0));
        setParameter(downspout::xoxolo::kParamResolution, static_cast<float>(static_cast<int>(pattern_.resolution)));
        pushPatternState();
        repaint();
    }

    void cycleChannel(const int direction = 1)
    {
        int channel = pattern_.channel + direction;
        if (channel > 16)
            channel = 1;
        if (channel < 1)
            channel = 16;
        pattern_.channel = channel;
        setParameter(downspout::xoxolo::kParamChannel, static_cast<float>(pattern_.channel));
        pushPatternState();
        repaint();
    }

    void nudgeLaneNote(const int lane, const int direction)
    {
        if (lane < 0 || lane >= downspout::xoxolo::kLaneCount)
            return;
        auto& laneState = pattern_.lanes[static_cast<std::size_t>(lane)];
        laneState.midiNote = clampi(laneState.midiNote + direction, 0, 127);
        pushPatternState();
        repaint();
    }

    void triggerPreview(const int lane)
    {
        setParameter(downspout::xoxolo::kParamPreviewLane, static_cast<float>(lane));
        triggerParameter(downspout::xoxolo::kParamPreview);
        pulseFrames_ = 8;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XoxoloUI)
};

UI* createUI()
{
    return new XoxoloUI();
}

END_NAMESPACE_DISTRHO
