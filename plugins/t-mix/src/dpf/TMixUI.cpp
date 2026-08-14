#include "DistrhoUI.hpp"

#include "t_mix_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::tmix;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

[[nodiscard]] float clampf(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] float levelToUnit(float decibels)
{
    return clampf((decibels - kMinimumLevelDb) / (kMaximumLevelDb - kMinimumLevelDb), 0.0f, 1.0f);
}

[[nodiscard]] float unitToLevel(float unit)
{
    return kMinimumLevelDb + clampf(unit, 0.0f, 1.0f) * (kMaximumLevelDb - kMinimumLevelDb);
}

[[nodiscard]] float meterToUnit(float peak)
{
    if (peak <= 0.0001f)
        return 0.0f;
    const float decibels = 20.0f * std::log10(peak);
    return clampf((decibels + 60.0f) / 60.0f, 0.0f, 1.0f);
}

}  // namespace

class TMixUI : public UI
{
public:
    TMixUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values_.size()) {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        drawBackground(width, height);
        drawHeader(width);

        const float left = 24.0f;
        const float top = 104.0f;
        const float bottom = 24.0f;
        const float gap = 10.0f;
        const float masterWidth = 160.0f;
        const float stripWidth = (width - left * 2.0f - masterWidth - gap * 8.0f) / 8.0f;
        const float stripHeight = height - top - bottom;

        for (uint32_t channel = 0; channel < kInputChannelCount; ++channel) {
            const float x = left + channel * (stripWidth + gap);
            drawChannel(channel, {x, top, stripWidth, stripHeight});
        }
        const float masterX = left + 8.0f * (stripWidth + gap);
        drawMaster({masterX, top, masterWidth, stripHeight});
    }

    bool onMouse(const MouseEvent& event) override
    {
        if (event.button != 1)
            return false;
        if (!event.press) {
            if (dragParameter_ >= 0)
                editParameter(static_cast<uint32_t>(dragParameter_), false);
            dragParameter_ = -1;
            return false;
        }

        const float x = static_cast<float>(event.pos.getX());
        const float y = static_cast<float>(event.pos.getY());
        for (uint32_t channel = 0; channel < kInputChannelCount; ++channel) {
            if (muteRects_[channel].contains(x, y)) {
                toggle(kParamMuteBase + channel);
                return true;
            }
            if (soloRects_[channel].contains(x, y)) {
                toggle(kParamSoloBase + channel);
                return true;
            }
            if (levelRects_[channel].contains(x, y)) {
                beginDrag(kParamLevelBase + channel);
                updateVertical(kParamLevelBase + channel, levelRects_[channel], y);
                return true;
            }
            if (panRects_[channel].contains(x, y)) {
                beginDrag(kParamPanBase + channel);
                updatePan(channel, x);
                return true;
            }
        }
        if (masterRect_.contains(x, y)) {
            beginDrag(kParamMaster);
            updateVertical(kParamMaster, masterRect_, y);
            return true;
        }
        if (producerChannelRect_.contains(x, y)) {
            const int current = static_cast<int>(std::lround(values_[kParamProducerControlChannel]));
            commit(kParamProducerControlChannel, static_cast<float>((current + 1) % 17));
            return true;
        }
        if (producerGateRect_.contains(x, y)) {
            toggle(kParamRequireProducerGate);
            return true;
        }
        if (producerSlewRect_.contains(x, y)) {
            beginDrag(kParamProducerSlew);
            updateProducerSlew(x);
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& event) override
    {
        if (dragParameter_ < 0)
            return false;
        const uint32_t parameter = static_cast<uint32_t>(dragParameter_);
        const float x = static_cast<float>(event.pos.getX());
        const float y = static_cast<float>(event.pos.getY());
        if (parameter == kParamMaster)
            updateVertical(parameter, masterRect_, y);
        else if (parameter == kParamProducerSlew)
            updateProducerSlew(x);
        else if (parameter >= kParamLevelBase && parameter < kParamPanBase)
            updateVertical(parameter, levelRects_[parameter - kParamLevelBase], y);
        else if (parameter >= kParamPanBase && parameter < kParamMuteBase)
            updatePan(parameter - kParamPanBase, x);
        return true;
    }

    bool onScroll(const ScrollEvent& event) override
    {
        const float x = static_cast<float>(event.pos.getX());
        const float y = static_cast<float>(event.pos.getY());
        const float direction = event.delta.getY() > 0.0f ? 1.0f : -1.0f;
        for (uint32_t channel = 0; channel < kInputChannelCount; ++channel) {
            if (levelRects_[channel].contains(x, y)) {
                commit(kParamLevelBase + channel,
                       clampf(values_[kParamLevelBase + channel] + direction, kMinimumLevelDb, kMaximumLevelDb));
                return true;
            }
            if (panRects_[channel].contains(x, y)) {
                commit(kParamPanBase + channel,
                       clampf(values_[kParamPanBase + channel] + direction * 0.05f, -1.0f, 1.0f));
                return true;
            }
        }
        if (masterRect_.contains(x, y)) {
            commit(kParamMaster, clampf(values_[kParamMaster] + direction, kMinimumLevelDb, kMaximumLevelDb));
            return true;
        }
        if (producerSlewRect_.contains(x, y)) {
            commit(kParamProducerSlew, clampf(values_[kParamProducerSlew] + direction * 5.0f, 0.0f, 500.0f));
            return true;
        }
        if (producerChannelRect_.contains(x, y)) {
            const float next = clampf(values_[kParamProducerControlChannel] + direction, 0.0f, 16.0f);
            commit(kParamProducerControlChannel, next);
            return true;
        }
        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kInputChannelCount> levelRects_ {};
    std::array<Rect, kInputChannelCount> panRects_ {};
    std::array<Rect, kInputChannelCount> muteRects_ {};
    std::array<Rect, kInputChannelCount> soloRects_ {};
    Rect masterRect_ {};
    Rect producerSlewRect_ {};
    Rect producerChannelRect_ {};
    Rect producerGateRect_ {};
    int dragParameter_ = -1;

    void drawBackground(float width, float height)
    {
        beginPath();
        rect(0.0f, 0.0f, width, height);
        fillColor(8, 12, 17, 255);
        fill();
        closePath();

        beginPath();
        rect(0.0f, 0.0f, width, 88.0f);
        fillColor(18, 31, 39, 255);
        fill();
        closePath();
    }

    void drawHeader(float width)
    {
        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(239, 243, 242, 255);
        text(26.0f, 17.0f, "T-Mix", nullptr);

        fontSize(12.0f);
        fillColor(135, 158, 164, 255);
        text(27.0f, 56.0f,
             DOWNSPOUT_PLUGIN_VERSION_STRING "  |  Eight mono inputs to stereo",
             nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(104, 135, 142, 255);
        text(width - 26.0f, 43.0f, "PRODUCER BUS CC19 + CC20-27 / MIDI THRU", nullptr);
    }

    void drawPanel(const Rect& rect, bool master)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 12.0f);
        fillColor(master ? 23 : 16, master ? 31 : 23, master ? 38 : 29, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 12.0f);
        strokeColor(master ? 94 : 47, master ? 125 : 67, master ? 132 : 75, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();
    }

    void drawChannel(uint32_t channel, const Rect& strip)
    {
        drawPanel(strip, false);
        char label[16];
        std::snprintf(label, sizeof(label), "CH %u", channel + 1);
        fontSize(14.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(209, 219, 218, 255);
        text(strip.x + strip.w * 0.5f, strip.y + 14.0f, label, nullptr);

        const Rect meter {strip.x + 18.0f, strip.y + 42.0f, strip.w - 36.0f, 12.0f};
        drawMeter(meter, values_[kParamMeterBase + channel]);
        drawProducerGain({strip.x + 18.0f, strip.y + 61.0f, strip.w - 36.0f, 5.0f},
                         values_[kParamProducerGainBase + channel]);

        panRects_[channel] = {strip.x + (strip.w - 64.0f) * 0.5f, strip.y + 70.0f, 64.0f, 70.0f};
        drawPan(panRects_[channel], values_[kParamPanBase + channel]);

        const float buttonHeight = 32.0f;
        const float buttonGap = 8.0f;
        const float buttonY = strip.y + strip.h - buttonHeight * 2.0f - buttonGap - 16.0f;
        const float faderTop = strip.y + 158.0f;
        const float faderBottom = buttonY - 22.0f;
        levelRects_[channel] = {strip.x + 18.0f, faderTop, strip.w - 36.0f, faderBottom - faderTop};
        drawFader(levelRects_[channel], values_[kParamLevelBase + channel], false);

        muteRects_[channel] = {strip.x + 12.0f, buttonY, strip.w - 24.0f, buttonHeight};
        soloRects_[channel] = {strip.x + 12.0f, buttonY + buttonHeight + buttonGap, strip.w - 24.0f, buttonHeight};
        drawButton(muteRects_[channel], "MUTE", values_[kParamMuteBase + channel] >= 0.5f, false);
        drawButton(soloRects_[channel], "SOLO", values_[kParamSoloBase + channel] >= 0.5f, true);
    }

    void drawMaster(const Rect& strip)
    {
        drawPanel(strip, true);
        fontSize(14.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(229, 221, 192, 255);
        text(strip.x + strip.w * 0.5f, strip.y + 14.0f, "MASTER", nullptr);

        drawProducerStatus({strip.x + 14.0f, strip.y + 43.0f, strip.w - 28.0f, 24.0f});

        producerChannelRect_ = {strip.x + 14.0f, strip.y + 76.0f, strip.w - 28.0f, 30.0f};
        drawProducerChannel(producerChannelRect_);
        producerGateRect_ = {strip.x + 14.0f, strip.y + 114.0f, strip.w - 28.0f, 30.0f};
        drawProducerGate(producerGateRect_);
        producerSlewRect_ = {strip.x + 14.0f, strip.y + 153.0f, strip.w - 28.0f, 35.0f};
        drawProducerSlew(producerSlewRect_);

        masterRect_ = {strip.x + 38.0f, strip.y + 224.0f, strip.w - 76.0f, strip.h - 264.0f};
        drawFader(masterRect_, values_[kParamMaster], true);
    }

    void drawProducerStatus(const Rect& rect)
    {
        const bool active = values_[kParamStatusProducerActive] >= 0.5f;
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 5.0f);
        fillColor(active ? 31 : 15, active ? 72 : 39, active ? 63 : 44, 255);
        fill();
        beginPath();
        circle(rect.x + 13.0f, rect.y + rect.h * 0.5f, 4.0f);
        fillColor(active ? 91 : 67, active ? 211 : 78, active ? 170 : 83, 255);
        fill();
        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(active ? 211 : 123, active ? 229 : 142, active ? 219 : 146, 255);
        text(rect.x + 24.0f, rect.y + rect.h * 0.5f, active ? "PRODUCER ACTIVE" : "WAITING FOR BUS", nullptr);
    }

    void drawProducerChannel(const Rect& rect)
    {
        char label[28] {};
        const int channel = static_cast<int>(std::lround(values_[kParamProducerControlChannel]));
        std::snprintf(label, sizeof(label), channel == 0 ? "LISTEN: OMNI" : "LISTEN: CH %d", channel);
        drawButton(rect, label, false, false);
    }

    void drawProducerGate(const Rect& rect)
    {
        drawButton(rect, values_[kParamRequireProducerGate] >= 0.5f ? "CC19 GATE: ON" : "CC19 GATE: OFF",
                   values_[kParamRequireProducerGate] >= 0.5f, false);
    }

    void drawProducerGain(const Rect& rect, float gain)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 2.0f);
        fillColor(7, 10, 13, 255);
        fill();
        beginPath();
        roundedRect(rect.x, rect.y, std::max(1.0f, rect.w * clampf(gain, 0.0f, 1.0f)), rect.h, 2.0f);
        fillColor(87, 173, 211, 230);
        fill();
    }

    void drawProducerSlew(const Rect& rect)
    {
        char value[24];
        std::snprintf(value, sizeof(value), "AUTO %d ms",
                      static_cast<int>(std::lround(values_[kParamProducerSlew])));
        fontSize(9.5f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(126, 154, 163, 255);
        text(rect.x + rect.w * 0.5f, rect.y, value, nullptr);
        const float trackY = rect.y + 21.0f;
        beginPath();
        roundedRect(rect.x, trackY, rect.w, 5.0f, 2.0f);
        fillColor(7, 10, 13, 255);
        fill();
        beginPath();
        roundedRect(rect.x, trackY, std::max(2.0f, rect.w * values_[kParamProducerSlew] / 500.0f), 5.0f, 2.0f);
        fillColor(87, 173, 211, 230);
        fill();
    }

    void drawMeter(const Rect& rect, float peak)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 4.0f);
        fillColor(5, 8, 11, 255);
        fill();
        closePath();

        const float unit = meterToUnit(peak);
        if (unit > 0.0f) {
            const float active = rect.w * unit;
            beginPath();
            roundedRect(rect.x + 2.0f, rect.y + 2.0f, std::max(1.0f, active - 4.0f), rect.h - 4.0f, 3.0f);
            if (unit > 0.9f)
                fillColor(224, 82, 65, 255);
            else if (unit > 0.72f)
                fillColor(224, 174, 62, 255);
            else
                fillColor(65, 188, 128, 255);
            fill();
            closePath();
        }
    }

    void drawPan(const Rect& rect, float pan)
    {
        const float cx = rect.x + rect.w * 0.5f;
        const float cy = rect.y + 30.0f;
        const float radius = 22.0f;
        beginPath();
        circle(cx, cy, radius);
        fillColor(34, 46, 52, 255);
        fill();
        strokeColor(72, 98, 104, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        const float angle = (-135.0f + (pan + 1.0f) * 135.0f) * 3.14159265358979323846f / 180.0f;
        beginPath();
        moveTo(cx, cy);
        lineTo(cx + std::cos(angle) * 16.0f, cy + std::sin(angle) * 16.0f);
        strokeColor(91, 211, 170, 255);
        strokeWidth(3.0f);
        stroke();
        closePath();

        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(122, 146, 150, 255);
        text(cx, rect.y + 57.0f, "PAN", nullptr);
    }

    void drawFader(const Rect& rect, float decibels, bool master)
    {
        const float cx = rect.x + rect.w * 0.5f;
        beginPath();
        roundedRect(cx - 4.0f, rect.y, 8.0f, rect.h, 4.0f);
        fillColor(5, 8, 11, 255);
        fill();
        closePath();

        const float unit = levelToUnit(decibels);
        const float knobY = rect.y + (1.0f - unit) * rect.h;
        beginPath();
        roundedRect(rect.x, knobY - 10.0f, rect.w, 20.0f, 5.0f);
        fillColor(master ? 211 : 83, master ? 166 : 174, master ? 73 : 153, 255);
        fill();
        closePath();

        char value[24];
        if (decibels <= kMinimumLevelDb + 0.01f)
            std::snprintf(value, sizeof(value), "-inf");
        else
            std::snprintf(value, sizeof(value), "%+.1f dB", decibels);
        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fillColor(144, 160, 163, 255);
        text(cx, rect.y - 8.0f, value, nullptr);
    }

    void drawButton(const Rect& rect, const char* label, bool active, bool solo)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 6.0f);
        if (active && solo)
            fillColor(201, 150, 49, 255);
        else if (active)
            fillColor(179, 65, 65, 255);
        else
            fillColor(31, 42, 47, 255);
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(active ? 250 : 141, active ? 247 : 158, active ? 224 : 160, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, label, nullptr);
    }

    void beginDrag(uint32_t parameter)
    {
        dragParameter_ = static_cast<int>(parameter);
        editParameter(parameter, true);
    }

    void updateVertical(uint32_t parameter, const Rect& rect, float y)
    {
        const float unit = 1.0f - clampf((y - rect.y) / rect.h, 0.0f, 1.0f);
        const float value = unitToLevel(unit);
        values_[parameter] = value;
        setParameterValue(parameter, value);
        repaint();
    }

    void updatePan(uint32_t channel, float x)
    {
        const Rect& rect = panRects_[channel];
        const float value = clampf(((x - rect.x) / rect.w) * 2.0f - 1.0f, -1.0f, 1.0f);
        const uint32_t parameter = kParamPanBase + channel;
        values_[parameter] = value;
        setParameterValue(parameter, value);
        repaint();
    }

    void updateProducerSlew(float x)
    {
        const float unit = clampf((x - producerSlewRect_.x) / producerSlewRect_.w, 0.0f, 1.0f);
        const float value = unit * 500.0f;
        values_[kParamProducerSlew] = value;
        setParameterValue(kParamProducerSlew, value);
        repaint();
    }

    void toggle(uint32_t parameter)
    {
        commit(parameter, values_[parameter] >= 0.5f ? 0.0f : 1.0f);
    }

    void commit(uint32_t parameter, float value)
    {
        values_[parameter] = value;
        editParameter(parameter, true);
        setParameterValue(parameter, value);
        editParameter(parameter, false);
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TMixUI)
};

UI* createUI()
{
    return new TMixUI();
}

END_NAMESPACE_DISTRHO
