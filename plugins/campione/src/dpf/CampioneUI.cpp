#include "DistrhoUI.hpp"

#include "campione_params.hpp"
#include "campione_serialization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

using downspout::campione::kParamCrossfadeDuration;
using downspout::campione::kParamMasterVolume;
using downspout::campione::kParamMidiChannel;
using downspout::campione::kParamPitchBendRange;
using downspout::campione::kParameterCount;
using downspout::campione::kStateKeyZoneLoad;
using downspout::campione::kStateKeyZones;

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

// Lightweight zone display info tracked on the UI side
struct ZoneEntry {
    std::string path;
    int rootNote = 60;
    int rangeLow = 0;
    int rangeHigh = 127;
    bool loopEnabled = false;
};

[[nodiscard]] std::string basename(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] std::string midiNoteName(int note) {
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    if (note < 0 || note > 127) return "?";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", names[note % 12], note / 12 - 1);
    return buf;
}

constexpr float kMargin = 12.0f;
constexpr float kHeaderH = 44.0f;
constexpr float kRowH = 28.0f;
constexpr float kFooterH = 52.0f;
constexpr float kScrollW = 0.0f;  // no scroll bar for now

}  // namespace

class CampioneUI : public UI
{
public:
    CampioneUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamMasterVolume]      = 0.8f;
        values_[kParamMidiChannel]       = 0.0f;
        values_[kParamCrossfadeDuration] = 20.0f;
        values_[kParamPitchBendRange]    = 2.0f;

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

    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyZoneLoad) == 0 && value && value[0] != '\0')
        {
            // A zone was loaded; add it to our local display list.
            // Root note will be confirmed once zones state is synced from host.
            ZoneEntry e;
            e.path = value;
            zones_.push_back(e);
            repaint();
            return;
        }

        if (std::strcmp(key, kStateKeyZones) == 0)
        {
            // Full zone list from project load — rebuild display list.
            rebuildFromZonesState(value ? value : "");
            repaint();
            return;
        }
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());

        // Background
        beginPath();
        rect(0, 0, W, H);
        fillColor(28, 28, 32);
        fill();
        closePath();

        drawHeader(W);
        drawZoneList(W, H);
        drawFooter(W, H);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (!ev.press) return false;
        const float px = static_cast<float>(ev.pos.getX());
        const float py = static_cast<float>(ev.pos.getY());

        // Load WAV button
        if (loadBtn_.contains(px, py)) {
            requestStateFile(kStateKeyZoneLoad);
            return true;
        }

        // Loop toggles in zone rows
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());
        const float listY = kHeaderH + kMargin;
        const float listH = H - kHeaderH - kFooterH - kMargin * 2;

        for (std::size_t i = 0; i < zones_.size(); ++i) {
            const float ry = listY + static_cast<float>(i) * kRowH;
            if (ry + kRowH < listY || ry > listY + listH) continue;

            const Rect loopR { W - 120.0f, ry + 4.0f, 44.0f, 20.0f };
            if (loopR.contains(px, py)) {
                zones_[i].loopEnabled = !zones_[i].loopEnabled;
                repaint();
                return true;
            }
        }

        // Volume knob drag (simplified: click sets value from click position)
        if (volKnob_.contains(px, py)) {
            draggingParam_ = kParamMasterVolume;
            dragStartY_    = py;
            dragStartVal_  = values_[kParamMasterVolume];
            return true;
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (draggingParam_ < 0) return false;
        const float dy = dragStartY_ - static_cast<float>(ev.pos.getY());
        float newVal = dragStartVal_ + dy / 100.0f;
        newVal = std::clamp(newVal, 0.0f, 1.0f);
        values_[draggingParam_] = newVal;
        setParameterValue(static_cast<uint32_t>(draggingParam_), newVal);
        repaint();
        return true;
    }

    bool onMouse(const MouseEvent& ev, bool released)
    {
        if (released) { draggingParam_ = -1; return false; }
        return onMouse(ev);
    }

    void onNanoDisplayEnd()
    {
        (void)0; // no-op
    }

private:
    void drawHeader(float W)
    {
        // Title
        fontSize(18.0f);
        fontFace("sans");
        fillColor(220, 200, 160);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(kMargin, kHeaderH / 2.0f, "CAMPIONE", nullptr);

        // Volume knob
        const float kx = W - 120.0f;
        const float ky = kHeaderH / 2.0f;
        volKnob_ = { kx - 16.0f, ky - 16.0f, 32.0f, 32.0f };
        drawKnob(kx, ky, 14.0f, values_[kParamMasterVolume], 0.0f, 1.0f, "VOL");

        // MIDI channel label
        const int ch = static_cast<int>(std::lround(values_[kParamMidiChannel]));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "CH: %s", ch == 0 ? "All" : std::to_string(ch).c_str());
        fontSize(11.0f);
        fillColor(160, 160, 180);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(kx + 24.0f, ky, buf, nullptr);
    }

    void drawKnob(float cx, float cy, float r, float value, float minVal, float maxVal,
                  const char* label)
    {
        const float norm = maxVal > minVal ? (value - minVal) / (maxVal - minVal) : 0.0f;
        const float startAngle = static_cast<float>(3.14159265f * 0.75f);
        const float range      = static_cast<float>(3.14159265f * 1.5f);
        const float angle      = startAngle + norm * range;

        // Track
        beginPath();
        arc(cx, cy, r, startAngle, startAngle + range, CW);
        strokeColor(60, 60, 70);
        strokeWidth(3.0f);
        stroke();
        closePath();

        // Value arc
        beginPath();
        arc(cx, cy, r, startAngle, angle, CW);
        strokeColor(200, 160, 80);
        strokeWidth(3.0f);
        stroke();
        closePath();

        // Indicator line
        beginPath();
        moveTo(cx, cy);
        lineTo(cx + std::cos(angle) * r, cy + std::sin(angle) * r);
        strokeColor(240, 200, 100);
        strokeWidth(2.0f);
        stroke();
        closePath();

        // Label
        fontSize(9.0f);
        fillColor(160, 160, 180);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(cx, cy + r + 3.0f, label, nullptr);
    }

    void drawZoneList(float W, float H)
    {
        const float listY = kHeaderH + kMargin;
        const float listH = H - kHeaderH - kFooterH - kMargin * 2.0f;

        // List border
        beginPath();
        rect(kMargin, listY, W - kMargin * 2.0f, listH);
        strokeColor(60, 60, 72);
        strokeWidth(1.0f);
        stroke();
        closePath();

        // Column headers
        fontSize(10.0f);
        fillColor(120, 120, 140);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(kMargin + 6.0f,       listY + 10.0f, "#",      nullptr);
        text(kMargin + 26.0f,      listY + 10.0f, "Root",   nullptr);
        text(kMargin + 80.0f,      listY + 10.0f, "Range",  nullptr);
        text(W - 120.0f,           listY + 10.0f, "Loop",   nullptr);
        text(kMargin + 180.0f,     listY + 10.0f, "File",   nullptr);

        // Separator
        beginPath();
        moveTo(kMargin, listY + 20.0f);
        lineTo(W - kMargin, listY + 20.0f);
        strokeColor(50, 50, 62);
        stroke();
        closePath();

        // Zone rows
        const float rowArea = listH - 20.0f;
        for (std::size_t i = 0; i < zones_.size(); ++i) {
            const float ry = listY + 20.0f + static_cast<float>(i) * kRowH;
            if (ry + kRowH < listY || ry > listY + listH) break;

            const ZoneEntry& z = zones_[i];
            const bool even = (i % 2) == 0;
            beginPath();
            rect(kMargin + 1.0f, ry, W - kMargin * 2.0f - 1.0f, kRowH - 1.0f);
            fillColor(even ? 34 : 30, even ? 34 : 30, even ? 40 : 36);
            fill();
            closePath();

            fontSize(11.0f);
            fillColor(200, 200, 215);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            const float mid = ry + kRowH / 2.0f;

            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(i + 1));
            text(kMargin + 6.0f, mid, buf, nullptr);

            text(kMargin + 26.0f, mid, midiNoteName(z.rootNote).c_str(), nullptr);

            std::snprintf(buf, sizeof(buf), "%s\xe2\x80\x93%s",
                          midiNoteName(z.rangeLow).c_str(),
                          midiNoteName(z.rangeHigh).c_str());
            text(kMargin + 80.0f, mid, buf, nullptr);

            // Loop toggle button
            const Rect loopBtn { W - 120.0f, ry + 4.0f, 44.0f, 20.0f };
            beginPath();
            rect(loopBtn.x, loopBtn.y, loopBtn.w, loopBtn.h);
            fillColor(z.loopEnabled ? 60 : 40, z.loopEnabled ? 100 : 40, z.loopEnabled ? 60 : 40);
            fill();
            closePath();
            fontSize(10.0f);
            fillColor(z.loopEnabled ? 160 : 120, z.loopEnabled ? 220 : 120, z.loopEnabled ? 160 : 120);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(loopBtn.x + loopBtn.w / 2.0f, loopBtn.y + loopBtn.h / 2.0f,
                 z.loopEnabled ? "LOOP" : "OFF", nullptr);

            // Filename (truncated)
            fontSize(10.0f);
            fillColor(160, 160, 180);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            const std::string fname = basename(z.path);
            text(kMargin + 180.0f, mid, fname.c_str(), nullptr);

            (void)rowArea;
        }

        if (zones_.empty()) {
            fontSize(13.0f);
            fillColor(80, 80, 100);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(W / 2.0f, listY + listH / 2.0f, "No zones loaded — click Load WAV", nullptr);
        }
    }

    void drawFooter(float W, float H)
    {
        const float fy = H - kFooterH;

        // Separator
        beginPath();
        moveTo(kMargin, fy);
        lineTo(W - kMargin, fy);
        strokeColor(50, 50, 62);
        stroke();
        closePath();

        // Load WAV button
        loadBtn_ = { kMargin, fy + 12.0f, 100.0f, 28.0f };
        drawButton(loadBtn_, "Load WAV", 50, 80, 120);

        // Crossfade label + value
        char buf[48];
        std::snprintf(buf, sizeof(buf), "XFade: %.0f ms", values_[kParamCrossfadeDuration]);
        fontSize(11.0f);
        fillColor(160, 160, 180);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(kMargin + 116.0f, fy + 26.0f, buf, nullptr);

        // Zone count
        std::snprintf(buf, sizeof(buf), "%d zone%s",
                      static_cast<int>(zones_.size()),
                      zones_.size() == 1 ? "" : "s");
        fillColor(100, 100, 120);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(W - kMargin, fy + 26.0f, buf, nullptr);
    }

    void drawButton(const Rect& r, const char* label, int cr, int cg, int cb)
    {
        beginPath();
        rect(r.x, r.y, r.w, r.h);
        fillColor(static_cast<uint8_t>(cr), static_cast<uint8_t>(cg), static_cast<uint8_t>(cb));
        fill();
        closePath();
        fontSize(11.0f);
        fillColor(220, 220, 235);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(r.x + r.w / 2.0f, r.y + r.h / 2.0f, label, nullptr);
    }

    void rebuildFromZonesState(const std::string& text)
    {
        const auto metas = downspout::campione::deserializeZones(text);
        if (!metas.has_value()) return;

        zones_.clear();
        zones_.reserve(metas->size());
        for (const auto& z : *metas) {
            ZoneEntry e;
            e.path        = z.sourcePath;
            e.rootNote    = z.rootNote;
            e.rangeLow    = z.rangeLow;
            e.rangeHigh   = z.rangeHigh;
            e.loopEnabled = z.loopEnabled;
            zones_.push_back(std::move(e));
        }
    }

    std::array<float, kParameterCount> values_ {};
    std::vector<ZoneEntry> zones_;

    Rect loadBtn_ {};
    Rect volKnob_ {};

    int   draggingParam_ = -1;
    float dragStartY_    = 0.0f;
    float dragStartVal_  = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CampioneUI)
};

UI* createUI()
{
    return new CampioneUI();
}

END_NAMESPACE_DISTRHO
