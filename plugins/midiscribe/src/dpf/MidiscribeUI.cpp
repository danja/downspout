#include "DistrhoUI.hpp"
#include "midiscribe_params.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

using namespace downspout::midiscribe;

namespace {

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

} // namespace

// ---------------------------------------------------------------------------

class MidiscribeUI : public UI
{
public:
    MidiscribeUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamCaptureBeats] = static_cast<float>(kDefaultCaptureBeatIndex);
        values_[kParamBar]          = 1.0f;
        values_[kParamBeat]         = 1.0f;
        values_[kParamBpm]          = static_cast<float>(kDefaultBpm);

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < kParamCount) {
            values_[index] = value;
            repaint();
        }
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyExportPath) == 0) {
            exportPath_ = (value != nullptr && value[0] != '\0') ? value : kDefaultExportPath;
            awaitingFile_ = false;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());
        drawBackground(w, h);
        drawHeader(w);
        drawTransport(w);
        drawControls(w);
        drawFilePath(w);
        drawCaptureWindow(w);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || !ev.press) return false;

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (recRect_.contains(x, y)) {
            commitParameter(kParamArmed, armed() ? 0.0f : 1.0f);
            return true;
        }
        if (writeRect_.contains(x, y)) {
            commitParameter(kParamWrite, 1.0f);
            return true;
        }
        if (browseRect_.contains(x, y)) {
            awaitingFile_ = requestStateFile(kStateKeyExportPath);
            return true;
        }
        for (int i = 0; i < kCaptureBeatCount; ++i) {
            if (captureBtnRects_[i].contains(x, y)) {
                commitParameter(kParamCaptureBeats, static_cast<float>(i));
                return true;
            }
        }
        return false;
    }

private:
    std::array<float, kParamCount> values_ {};
    std::string exportPath_ { kDefaultExportPath };
    bool awaitingFile_ = false;

    Rect recRect_ {}, writeRect_ {}, browseRect_ {};
    std::array<Rect, kCaptureBeatCount> captureBtnRects_ {};

    bool  armed()        const { return values_[kParamArmed]     >= 0.5f; }
    bool  playing()      const { return values_[kParamIsPlaying] >= 0.5f; }
    int   bar()          const { return static_cast<int>(values_[kParamBar]  + 0.5f); }
    int   beat()         const { return static_cast<int>(values_[kParamBeat] + 0.5f); }
    float bpm()          const { return values_[kParamBpm]; }
    int   captureIndex() const { return static_cast<int>(values_[kParamCaptureBeats] + 0.5f); }

    void commitParameter(uint32_t index, float value)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    // -----------------------------------------------------------------------

    void drawBackground(float w, float h)
    {
        beginPath();
        fillColor(10, 16, 24, 255);
        rect(0.0f, 0.0f, w, h);
        fill();
        closePath();
    }

    void drawHeader(float w)
    {
        beginPath();
        fillColor(19, 37, 51, 255);
        rect(0.0f, 0.0f, w, 56.0f);
        fill();
        closePath();

        fontSize(21.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(239, 242, 245, 255);
        text(20.0f, 28.0f, "MIDISCRIBE", nullptr);

        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(100, 150, 180, 255);
        text(w - 20.0f, 28.0f, "Downspout", nullptr);
    }

    void drawTransport(float w)
    {
        constexpr float y = 56.0f, h = 48.0f, pad = 20.0f;

        beginPath();
        fillColor(12, 20, 30, 255);
        rect(0.0f, y, w, h);
        fill();
        closePath();

        beginPath();
        strokeColor(28, 48, 66, 255);
        strokeWidth(1.0f);
        moveTo(0.0f, y);
        lineTo(w, y);
        stroke();
        closePath();

        // Bar / beat
        char bbtBuf[32];
        std::snprintf(bbtBuf, sizeof(bbtBuf), "BAR %03d  BEAT %d", bar(), beat());
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(170, 205, 225, 255);
        text(pad, y + h * 0.5f, bbtBuf, nullptr);

        // BPM (centre)
        char bpmBuf[24];
        std::snprintf(bpmBuf, sizeof(bpmBuf), "%.1f BPM", bpm());
        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(130, 170, 195, 255);
        text(w * 0.5f, y + h * 0.5f, bpmBuf, nullptr);

        // Playing indicator (right)
        const bool isPlaying = playing();
        const float dotX = w - pad - 78.0f;
        const float dotY = y + h * 0.5f;

        beginPath();
        circle(dotX, dotY, 5.0f);
        fillColor(isPlaying ? 60 : 50, isPlaying ? 210 : 90, isPlaying ? 60 : 90, 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(isPlaying ? 60 : 100, isPlaying ? 210 : 130, isPlaying ? 60 : 140, 255);
        text(dotX + 9.0f, dotY, isPlaying ? "PLAYING" : "STOPPED", nullptr);
    }

    void drawControls(float w)
    {
        constexpr float secY = 104.0f, secH = 76.0f, pad = 20.0f;
        constexpr float btnH = 48.0f;
        const float btnY = secY + (secH - btnH) * 0.5f;

        const bool isArmed = armed();

        // Record / Stop button
        constexpr float recW = 160.0f;
        recRect_ = {pad, btnY, recW, btnH};

        beginPath();
        roundedRect(recRect_.x, recRect_.y, recRect_.w, recRect_.h, 6.0f);
        fillColor(isArmed ? 96 : 18, isArmed ? 16 : 18, isArmed ? 16 : 28, 255);
        fill();
        strokeColor(isArmed ? 228 : 70, isArmed ? 55 : 90, isArmed ? 55 : 110, 255);
        strokeWidth(1.5f);
        stroke();
        closePath();

        // Dot
        const float dotX = recRect_.x + 22.0f;
        const float dotY = recRect_.y + recRect_.h * 0.5f;
        beginPath();
        circle(dotX, dotY, 6.0f);
        fillColor(isArmed ? 228 : 110, isArmed ? 55 : 70, isArmed ? 55 : 70, 255);
        fill();
        closePath();

        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(235, 225, 225, 255);
        text(dotX + 14.0f, dotY, isArmed ? "STOP" : "RECORD", nullptr);

        // Write / Save button
        constexpr float writeW = 140.0f;
        const float writeX = pad + recW + 16.0f;
        writeRect_ = {writeX, btnY, writeW, btnH};

        beginPath();
        roundedRect(writeRect_.x, writeRect_.y, writeRect_.w, writeRect_.h, 6.0f);
        fillColor(16, 34, 22, 255);
        fill();
        strokeColor(55, 150, 75, 255);
        strokeWidth(1.5f);
        stroke();
        closePath();

        fontSize(14.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(150, 225, 170, 255);
        text(writeRect_.x + writeRect_.w * 0.5f,
             writeRect_.y + writeRect_.h * 0.5f,
             "WRITE FILE", nullptr);
    }

    void drawFilePath(float w)
    {
        constexpr float secY = 180.0f, secH = 44.0f, pad = 20.0f;
        constexpr float browseW = 88.0f;
        const float browseX = w - pad - browseW;
        const float pathW   = browseX - pad - 8.0f;
        const float boxY    = secY + 8.0f;
        const float boxH    = secH - 16.0f;
        const float midY    = secY + secH * 0.5f;

        // Path display
        beginPath();
        roundedRect(pad, boxY, pathW, boxH, 4.0f);
        fillColor(12, 22, 32, 255);
        fill();
        strokeColor(36, 64, 84, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(150, 185, 205, 255);
        save();
        scissor(pad + 4.0f, boxY, pathW - 8.0f, boxH);
        text(pad + 8.0f, midY, exportPath_.c_str(), nullptr);
        restore();

        // Browse button
        browseRect_ = {browseX, secY + 6.0f, browseW, secH - 12.0f};

        beginPath();
        roundedRect(browseRect_.x, browseRect_.y, browseRect_.w, browseRect_.h, 4.0f);
        fillColor(20, 38, 52, 255);
        fill();
        strokeColor(55, 95, 130, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(150, 195, 225, 255);
        text(browseRect_.x + browseRect_.w * 0.5f,
             browseRect_.y + browseRect_.h * 0.5f,
             awaitingFile_ ? "..." : "Browse", nullptr);
    }

    void drawCaptureWindow(float w)
    {
        constexpr float secY = 224.0f, secH = 44.0f, pad = 20.0f;
        const float midY = secY + secH * 0.5f;

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(110, 140, 160, 255);
        text(pad, midY, "WINDOW", nullptr);

        constexpr float btnW = 52.0f, btnH = 28.0f, gap = 6.0f;
        const float btnStartX = pad + 72.0f;
        const float btnY = secY + (secH - btnH) * 0.5f;
        const int ci = captureIndex();

        for (int i = 0; i < kCaptureBeatCount; ++i) {
            const float bx = btnStartX + static_cast<float>(i) * (btnW + gap);
            captureBtnRects_[i] = {bx, btnY, btnW, btnH};

            const bool active = (ci == i);
            beginPath();
            roundedRect(bx, btnY, btnW, btnH, 4.0f);
            fillColor(active ? 28 : 14, active ? 50 : 24, active ? 68 : 36, 255);
            fill();
            strokeColor(active ? 75 : 36, active ? 155 : 74, active ? 215 : 96, 255);
            strokeWidth(1.0f);
            stroke();
            closePath();

            char label[8];
            std::snprintf(label, sizeof(label), "%d", kCaptureBeatValues[i]);
            fontSize(12.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(active ? 155 : 95, active ? 215 : 135, active ? 255 : 155, 255);
            text(bx + btnW * 0.5f, btnY + btnH * 0.5f, label, nullptr);
        }

        // "beats" suffix label
        const float suffixX = btnStartX
            + static_cast<float>(kCaptureBeatCount) * (btnW + gap) - gap + 8.0f;
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(90, 120, 140, 255);
        text(suffixX, midY, "beats", nullptr);

        // Bottom separator
        beginPath();
        strokeColor(22, 40, 56, 255);
        strokeWidth(1.0f);
        moveTo(0.0f, static_cast<float>(getHeight()) - 1.0f);
        lineTo(w, static_cast<float>(getHeight()) - 1.0f);
        stroke();
        closePath();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiscribeUI)
};

// ---------------------------------------------------------------------------

UI* createUI()
{
    return new MidiscribeUI();
}

END_NAMESPACE_DISTRHO
