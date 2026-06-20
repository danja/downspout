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

constexpr int kNoteMenuColumns = 8;
constexpr int kNoteMenuRows = 16;
constexpr float kNoteMenuItemHeight = 22.0f;
constexpr float kNoteMenuItemWidth = 42.0f;

}  // namespace

class XoxoloUI : public UI
{
public:
    XoxoloUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        pattern_ = downspout::xoxolo::makeDefaultPattern();
        values_.fill(0.0f);
        values_[downspout::xoxolo::kParamSteps] = static_cast<float>(downspout::xoxolo::kDefaultSteps);
        values_[downspout::xoxolo::kParamResolution] = 2.0f;
        values_[downspout::xoxolo::kParamChannel] = 10.0f;
        values_[downspout::xoxolo::kParamNotePreset] =
            static_cast<float>(static_cast<int>(downspout::xoxolo::NotePresetId::downspout));
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
        if (index == downspout::xoxolo::kParamSteps ||
            index == downspout::xoxolo::kParamResolution ||
            index == downspout::xoxolo::kParamChannel) {
            applyParameterControlsToPattern(false);
        } else if (index == downspout::xoxolo::kParamNotePreset) {
            const auto preset = static_cast<downspout::xoxolo::NotePresetId>(
                clampi(static_cast<int>(std::lround(value)),
                       0,
                       static_cast<int>(downspout::xoxolo::NotePresetId::count) - 1));
            if (pattern_.notePreset != preset)
                downspout::xoxolo::applyNotePreset(pattern_, preset);
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
        values_[downspout::xoxolo::kParamSteps] = static_cast<float>(pattern_.totalSteps);
        values_[downspout::xoxolo::kParamResolution] = static_cast<float>(static_cast<int>(pattern_.resolution));
        values_[downspout::xoxolo::kParamChannel] = static_cast<float>(pattern_.channel);
        values_[downspout::xoxolo::kParamNotePreset] = static_cast<float>(static_cast<int>(pattern_.notePreset));
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
        drawNoteMenu();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press)
            return false;

        if (openNoteMenuLane_ >= 0 && handleNoteMenuClick(x, y))
            return true;

        const int laneCount = downspout::xoxolo::activeLaneCountForPreset(pattern_.notePreset);
        for (int lane = 0; lane < laneCount; ++lane) {
            if (previewRects_[static_cast<std::size_t>(lane)].contains(x, y)) {
                openNoteMenuLane_ = -1;
                triggerPreview(lane);
                return true;
            }
            if (noteRects_[static_cast<std::size_t>(lane)].contains(x, y)) {
                openNoteMenuLane_ = openNoteMenuLane_ == lane ? -1 : lane;
                repaint();
                return true;
            }
        }

        for (int lane = 0; lane < laneCount; ++lane) {
            for (int step = 0; step < pattern_.totalSteps; ++step) {
                if (cellRects_[static_cast<std::size_t>(lane)][static_cast<std::size_t>(step)].contains(x, y)) {
                    const bool active = !downspout::xoxolo::cellActive(pattern_, lane, step);
                    downspout::xoxolo::setCell(pattern_, lane, step, active);
                    pushPatternState();
                    openNoteMenuLane_ = -1;
                    repaint();
                    return true;
                }
            }
        }

        if (stepsRect_.contains(x, y)) {
            openNoteMenuLane_ = -1;
            cycleSteps();
            return true;
        }
        if (presetRect_.contains(x, y)) {
            openNoteMenuLane_ = -1;
            cyclePreset();
            return true;
        }
        if (resolutionRect_.contains(x, y)) {
            openNoteMenuLane_ = -1;
            cycleResolution();
            return true;
        }
        if (channelRect_.contains(x, y)) {
            openNoteMenuLane_ = -1;
            cycleChannel();
            return true;
        }
        if (clearRect_.contains(x, y)) {
            openNoteMenuLane_ = -1;
            downspout::xoxolo::clearPattern(pattern_);
            pushPatternState();
            triggerParameter(downspout::xoxolo::kParamClear);
            pulseFrames_ = 8;
            repaint();
            return true;
        }

        openNoteMenuLane_ = -1;
        repaint();
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        const int direction = ev.delta.getY() > 0.0f ? 1 : -1;

        const int laneCount = downspout::xoxolo::activeLaneCountForPreset(pattern_.notePreset);
        for (int lane = 0; lane < laneCount; ++lane) {
            if (noteRects_[static_cast<std::size_t>(lane)].contains(x, y)) {
                nudgeLaneNote(lane, direction);
                return true;
            }
        }
        if (stepsRect_.contains(x, y)) {
            cycleSteps(direction);
            return true;
        }
        if (presetRect_.contains(x, y)) {
            cyclePreset(direction);
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
    Rect presetRect_ {};
    Rect stepsRect_ {};
    Rect resolutionRect_ {};
    Rect channelRect_ {};
    Rect clearRect_ {};
    int pulseFrames_ = 0;
    int openNoteMenuLane_ = -1;

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
        const int laneCount = downspout::xoxolo::activeLaneCountForPreset(pattern_.notePreset);
        const float labelW = pattern_.notePreset == downspout::xoxolo::NotePresetId::avlDrumkits ? 112.0f : 82.0f;
        const float noteW = 52.0f;
        const float previewW = 28.0f;
        const float gap = 6.0f;
        const float rowGap = laneCount > 16 ? 2.0f : 5.0f;
        const float rowH = std::min(36.0f,
                                    (h - rowGap * static_cast<float>(laneCount - 1)) /
                                        static_cast<float>(laneCount));
        const float gridX = x + labelW;
        const float rightW = noteW + previewW + gap * 2.0f;
        const float stepGap = 3.0f;
        const float cellW = (w - labelW - rightW - stepGap * static_cast<float>(pattern_.totalSteps - 1)) /
                            static_cast<float>(pattern_.totalSteps);
        const int currentStep = clampi(static_cast<int>(std::lround(values_[downspout::xoxolo::kParamCurrentStep])),
                                       -1,
                                       downspout::xoxolo::kMaxSteps - 1);

        for (int lane = 0; lane < laneCount; ++lane) {
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

            const float previewX = gridX + static_cast<float>(pattern_.totalSteps) * (cellW + stepGap) + gap;
            previewRects_[static_cast<std::size_t>(lane)] = {previewX, rowY, previewW, rowH};
            noteRects_[static_cast<std::size_t>(lane)] = {previewX + previewW + gap, rowY, noteW, rowH};
            drawPreview(previewRects_[static_cast<std::size_t>(lane)]);
            drawNoteDropdown(noteRects_[static_cast<std::size_t>(lane)],
                             pattern_.lanes[static_cast<std::size_t>(lane)].midiNote,
                             openNoteMenuLane_ == lane);
        }

        for (int lane = laneCount; lane < downspout::xoxolo::kLaneCount; ++lane) {
            previewRects_[static_cast<std::size_t>(lane)] = {};
            noteRects_[static_cast<std::size_t>(lane)] = {};
            for (int step = 0; step < downspout::xoxolo::kMaxSteps; ++step)
                cellRects_[static_cast<std::size_t>(lane)][static_cast<std::size_t>(step)] = {};
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
        text(x + 9.0f,
             y + h * 0.5f,
             downspout::xoxolo::laneSpecsForPreset(pattern_.notePreset)[static_cast<std::size_t>(lane)].name,
             nullptr);
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

    void drawNoteDropdown(const Rect& rect, const int note, const bool open)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 6.0f);
        fillColor(open ? 47 : 31, open ? 57 : 38, open ? 65 : 43, 255);
        fill();
        closePath();

        char textValue[8];
        std::snprintf(textValue, sizeof(textValue), "%d", note);
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(224, 231, 234, 255);
        text(rect.x + rect.w * 0.42f, rect.y + rect.h * 0.5f, textValue, nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(140, 153, 160, 255);
        text(rect.x + rect.w - 7.0f, rect.y + rect.h * 0.5f, open ? "^" : "v", nullptr);
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

    Rect noteMenuRect() const
    {
        if (openNoteMenuLane_ < 0 || openNoteMenuLane_ >= downspout::xoxolo::kLaneCount)
            return {};

        const Rect& base = noteRects_[static_cast<std::size_t>(openNoteMenuLane_)];
        const float menuW = kNoteMenuItemWidth * static_cast<float>(kNoteMenuColumns);
        const float menuH = kNoteMenuItemHeight * static_cast<float>(kNoteMenuRows);
        const float margin = 8.0f;
        const float windowW = static_cast<float>(getWidth());
        const float windowH = static_cast<float>(getHeight());
        float menuX = base.x + base.w - menuW;
        float menuY = base.y + base.h + 4.0f;
        menuX = std::max(margin, std::min(menuX, std::max(margin, windowW - margin - menuW)));
        if (menuY + menuH > windowH - margin)
            menuY = base.y - menuH - 4.0f;
        menuY = std::max(margin, std::min(menuY, std::max(margin, windowH - margin - menuH)));
        return {menuX, menuY, menuW, menuH};
    }

    void drawNoteMenu()
    {
        if (openNoteMenuLane_ < 0)
            return;

        const Rect menu = noteMenuRect();
        const int selected = pattern_.lanes[static_cast<std::size_t>(openNoteMenuLane_)].midiNote;

        beginPath();
        roundedRect(menu.x, menu.y, menu.w, menu.h, 8.0f);
        fillColor(18, 23, 27, 248);
        fill();
        strokeColor(83, 96, 106, 230);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        for (int note = 0; note < 128; ++note) {
            const int col = note % kNoteMenuColumns;
            const int row = note / kNoteMenuColumns;
            const float itemX = menu.x + static_cast<float>(col) * kNoteMenuItemWidth;
            const float itemY = menu.y + static_cast<float>(row) * kNoteMenuItemHeight;
            if (note == selected) {
                beginPath();
                roundedRect(itemX + 3.0f, itemY + 3.0f, kNoteMenuItemWidth - 6.0f, kNoteMenuItemHeight - 6.0f, 5.0f);
                fillColor(224, 117, 76, 230);
                fill();
                closePath();
                fillColor(255, 249, 245, 255);
            } else {
                fillColor(192, 203, 209, 255);
            }

            char label[8];
            std::snprintf(label, sizeof(label), "%d", note);
            text(itemX + kNoteMenuItemWidth * 0.5f, itemY + kNoteMenuItemHeight * 0.5f + 1.0f, label, nullptr);
        }
    }

    void drawControls(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(24, 29, 33, 255);
        fill();
        closePath();

        presetRect_ = {x + 14.0f, y + 42.0f, w - 28.0f, 44.0f};
        stepsRect_ = {x + 14.0f, y + 102.0f, w - 28.0f, 44.0f};
        resolutionRect_ = {x + 14.0f, y + 162.0f, w - 28.0f, 44.0f};
        channelRect_ = {x + 14.0f, y + 222.0f, w - 28.0f, 44.0f};
        clearRect_ = {x + 14.0f, y + h - 58.0f, w - 28.0f, 42.0f};

        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(226, 232, 235, 255);
        text(x + 14.0f, y + 16.0f, "Pattern", nullptr);

        drawSelector(presetRect_, "Preset", downspout::xoxolo::notePresetName(pattern_.notePreset));

        char stepsValue[12];
        std::snprintf(stepsValue, sizeof(stepsValue), "%d", pattern_.totalSteps);
        drawSelector(stepsRect_, "Steps", stepsValue);
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
        const int steps = clampi(static_cast<int>(std::lround(values_[downspout::xoxolo::kParamSteps])),
                                 downspout::xoxolo::kMinSteps,
                                 downspout::xoxolo::kMaxSteps);
        const auto resolution = static_cast<downspout::xoxolo::ResolutionId>(
            clampi(static_cast<int>(std::lround(values_[downspout::xoxolo::kParamResolution])), 0, 2));
        const int channel = clampi(static_cast<int>(std::lround(values_[downspout::xoxolo::kParamChannel])), 1, 16);
        const auto preset = static_cast<downspout::xoxolo::NotePresetId>(
            clampi(static_cast<int>(std::lround(values_[downspout::xoxolo::kParamNotePreset])),
                   0,
                   static_cast<int>(downspout::xoxolo::NotePresetId::count) - 1));
        pattern_.notePreset = downspout::xoxolo::clampNotePreset(preset);
        pattern_.channel = channel;
        downspout::xoxolo::resizePattern(pattern_, steps, resolution, downspout::meterFromTimeSignature(4.0, 4.0));
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

    void cycleSteps(const int direction = 1)
    {
        int next = pattern_.totalSteps + direction;
        if (next > downspout::xoxolo::kMaxSteps)
            next = downspout::xoxolo::kMinSteps;
        if (next < downspout::xoxolo::kMinSteps)
            next = downspout::xoxolo::kMaxSteps;
        downspout::xoxolo::resizePattern(pattern_, next, pattern_.resolution, downspout::meterFromTimeSignature(4.0, 4.0));
        setParameter(downspout::xoxolo::kParamSteps, static_cast<float>(pattern_.totalSteps));
        pushPatternState();
        repaint();
    }

    void cycleResolution(const int direction = 1)
    {
        int next = static_cast<int>(pattern_.resolution);
        for (int attempt = 0; attempt < 3; ++attempt) {
            next = (next + direction + 3) % 3;
            pattern_.resolution = static_cast<downspout::xoxolo::ResolutionId>(next);
            break;
        }
        downspout::xoxolo::resizePattern(pattern_, pattern_.totalSteps, pattern_.resolution, downspout::meterFromTimeSignature(4.0, 4.0));
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

    void cyclePreset(const int direction = 1)
    {
        const int count = static_cast<int>(downspout::xoxolo::NotePresetId::count);
        int next = static_cast<int>(pattern_.notePreset);
        next = (next + direction + count) % count;
        const auto preset = static_cast<downspout::xoxolo::NotePresetId>(next);
        downspout::xoxolo::applyNotePreset(pattern_, preset);
        setParameter(downspout::xoxolo::kParamNotePreset, static_cast<float>(next));
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

    bool handleNoteMenuClick(const float x, const float y)
    {
        const Rect menu = noteMenuRect();
        if (menu.contains(x, y)) {
            const int col = clampi(static_cast<int>((x - menu.x) / kNoteMenuItemWidth), 0, kNoteMenuColumns - 1);
            const int row = clampi(static_cast<int>((y - menu.y) / kNoteMenuItemHeight), 0, kNoteMenuRows - 1);
            const int note = clampi(row * kNoteMenuColumns + col, 0, 127);
            pattern_.lanes[static_cast<std::size_t>(openNoteMenuLane_)].midiNote = note;
            openNoteMenuLane_ = -1;
            pushPatternState();
            repaint();
            return true;
        }

        if (!noteRects_[static_cast<std::size_t>(openNoteMenuLane_)].contains(x, y)) {
            openNoteMenuLane_ = -1;
            repaint();
            return true;
        }

        return false;
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
