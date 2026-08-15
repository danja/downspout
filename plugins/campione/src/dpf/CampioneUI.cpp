#include "DistrhoUI.hpp"

#include "campione_params.hpp"
#include "campione_serialization.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

using downspout::campione::kParamCrossfadeDuration;
using downspout::campione::kParamMasterVolume;
using downspout::campione::kParamMidiChannel;
using downspout::campione::kParamPitchBendRange;
using downspout::campione::kParamRecording;
using downspout::campione::kParameterCount;
using downspout::campione::kStateKeyZoneLoad;
using downspout::campione::kStateKeyZoneRemove;
using downspout::campione::kStateKeyZoneUpdate;
using downspout::campione::kStateKeyZones;

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct ZoneEntry {
    std::string path;
    int  rootNote    = 60;
    int  rangeLow    = 0;
    int  rangeHigh   = 127;
    bool loopEnabled = false;
};

enum DragField { kDragNone, kDragVol, kDragRoot, kDragRangeLow, kDragRangeHigh };

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

constexpr float kPad     = 20.0f;
constexpr float kHeaderH = 56.0f;
constexpr float kRowH    = 26.0f;
constexpr float kFooterH = 60.0f;

// Column x positions (left edge relative to kPad)
constexpr float kColNum    =  8.0f;
constexpr float kColRoot   = 28.0f;
constexpr float kColRange  = 80.0f;
constexpr float kColFile   = 192.0f;

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
        if (std::strcmp(key, kStateKeyZoneLoad) == 0 && value && value[0] != '\0') {
            ZoneEntry e;
            e.path = value;
            zones_.push_back(e);
            repaint();
            return;
        }
        if (std::strcmp(key, kStateKeyZones) == 0) {
            rebuildFromZonesState(value ? value : "");
            repaint();
            return;
        }
    }

    void uiIdle() override
    {
        if (recording_)
            repaint();
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());

        beginPath();
        fillColor(11, 16, 24, 255);
        rect(0.0f, 0.0f, W, H);
        fill();
        closePath();

        beginPath();
        fillColor(20, 29, 38, 255);
        rect(0.0f, 0.0f, W, kHeaderH + kPad);
        fill();
        closePath();

        drawHeader(W);
        drawZoneList(W, H);
        drawFooter(W, H);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;

        const float px = static_cast<float>(ev.pos.getX());
        const float py = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            // Commit zone field drag
            if (dragField_ != kDragNone && dragField_ != kDragVol && dragZoneIdx_ >= 0
                && dragZoneIdx_ < static_cast<int>(zones_.size()))
            {
                pushZoneUpdate(static_cast<std::size_t>(dragZoneIdx_));
            }
            dragField_    = kDragNone;
            dragZoneIdx_  = -1;
            return false;
        }

        // Load WAV
        if (loadBtn_.contains(px, py)) {
            requestStateFile(kStateKeyZoneLoad);
            return true;
        }

        // Record toggle
        if (recBtn_.contains(px, py)) {
            recording_ = !recording_;
            if (recording_)
                recordStart_ = std::chrono::steady_clock::now();
            editParameter(kParamRecording, true);
            setParameterValue(kParamRecording, recording_ ? 1.0f : 0.0f);
            editParameter(kParamRecording, false);
            repaint();
            return true;
        }

        // MIDI channel — click to cycle (0=All, 1–16)
        if (chBtn_.contains(px, py)) {
            const int ch = static_cast<int>(std::lround(values_[kParamMidiChannel]));
            const float next = static_cast<float>((ch + 1) % 17);
            editParameter(kParamMidiChannel, true);
            setParameterValue(kParamMidiChannel, next);
            editParameter(kParamMidiChannel, false);
            values_[kParamMidiChannel] = next;
            repaint();
            return true;
        }

        // Volume slider
        if (volSlider_.contains(px, py)) {
            dragField_ = kDragVol;
            updateVolFromX(px);
            return true;
        }

        // Zone row interactions
        const float W     = static_cast<float>(getWidth());
        const float H     = static_cast<float>(getHeight());
        const float listY = kHeaderH + kPad * 2.0f;
        const float listH = H - listY - kFooterH - kPad;
        const float rowsY = listY + 23.0f;

        for (std::size_t i = 0; i < zones_.size(); ++i) {
            const float ry = rowsY + static_cast<float>(i) * kRowH;
            if (ry > listY + listH) break;

            // Remove button
            const Rect removeR { W - kPad - 22.0f, ry + 4.0f, 18.0f, 18.0f };
            if (removeR.contains(px, py)) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(i));
                setState(kStateKeyZoneRemove, buf);
                zones_.erase(zones_.begin() + static_cast<std::ptrdiff_t>(i));
                repaint();
                return true;
            }

            // Loop toggle
            const Rect loopR { W - kPad - 76.0f, ry + 3.0f, 46.0f, 20.0f };
            if (loopR.contains(px, py)) {
                zones_[i].loopEnabled = !zones_[i].loopEnabled;
                pushZoneUpdate(i);
                repaint();
                return true;
            }

            // Root note — drag start
            const Rect rootR { kPad + kColRoot, ry, 46.0f, kRowH };
            if (rootR.contains(px, py)) {
                dragField_    = kDragRoot;
                dragZoneIdx_  = static_cast<int>(i);
                dragStartY_   = py;
                dragStartNote_ = zones_[i].rootNote;
                return true;
            }

            // Range low — left half of range cell
            const Rect rangeCell { kPad + kColRange, ry, 100.0f, kRowH };
            if (rangeCell.contains(px, py)) {
                if (px < rangeCell.x + rangeCell.w * 0.5f) {
                    dragField_     = kDragRangeLow;
                    dragStartNote_ = zones_[i].rangeLow;
                } else {
                    dragField_     = kDragRangeHigh;
                    dragStartNote_ = zones_[i].rangeHigh;
                }
                dragZoneIdx_ = static_cast<int>(i);
                dragStartY_  = py;
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float py = static_cast<float>(ev.pos.getY());
        const float px = static_cast<float>(ev.pos.getX());

        if (dragField_ == kDragVol) {
            updateVolFromX(px);
            return true;
        }

        if (dragField_ != kDragNone && dragZoneIdx_ >= 0
            && dragZoneIdx_ < static_cast<int>(zones_.size()))
        {
            const int delta = static_cast<int>((dragStartY_ - py) / 3.0f);
            const int note  = std::clamp(dragStartNote_ + delta, 0, 127);
            ZoneEntry& z    = zones_[static_cast<std::size_t>(dragZoneIdx_)];
            if (dragField_ == kDragRoot)      z.rootNote  = note;
            if (dragField_ == kDragRangeLow)  z.rangeLow  = std::min(note, z.rangeHigh);
            if (dragField_ == kDragRangeHigh) z.rangeHigh = std::max(note, z.rangeLow);
            repaint();
            return true;
        }

        return false;
    }

private:
    void updateVolFromX(float px)
    {
        const float t = std::clamp((px - volSlider_.x) / volSlider_.w, 0.0f, 1.0f);
        values_[kParamMasterVolume] = t;
        setParameterValue(kParamMasterVolume, t);
        repaint();
    }

    void pushZoneUpdate(std::size_t idx)
    {
        if (idx >= zones_.size()) return;
        const ZoneEntry& z = zones_[idx];
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d|%d|%d|%d|%d",
                      static_cast<int>(idx),
                      z.rootNote, z.rangeLow, z.rangeHigh,
                      z.loopEnabled ? 1 : 0);
        setState(kStateKeyZoneUpdate, buf);
    }

    void drawHeader(float W)
    {
        fontSize(28.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(240, 242, 246, 255);
        text(kPad, kPad, "Campione", nullptr);

        fontSize(12.0f);
        fillColor(140, 156, 168, 255);
        text(kPad, kPad + 32.0f, "Multi-zone sampler", nullptr);

        // Volume slider
        const float volX = W * 0.42f;
        const float volY = kPad + 8.0f;
        const float volW = W * 0.22f;

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 232, 236, 255);
        text(volX, volY, "Volume", nullptr);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f%%", values_[kParamMasterVolume] * 100.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(240, 205, 170, 255);
        text(volX + volW, volY, buf, nullptr);

        volSlider_ = { volX, volY + 20.0f, volW, 8.0f };

        beginPath();
        fillColor(37, 48, 58, 255);
        roundedRect(volSlider_.x, volSlider_.y, volSlider_.w, volSlider_.h, 4.0f);
        fill();
        closePath();

        const float t = values_[kParamMasterVolume];
        beginPath();
        fillColor(201, 118, 73, 255);
        roundedRect(volSlider_.x, volSlider_.y, std::max(8.0f, volSlider_.w * t), volSlider_.h, 4.0f);
        fill();
        closePath();

        beginPath();
        fillColor(248, 239, 229, 255);
        circle(volSlider_.x + volSlider_.w * t, volSlider_.y + volSlider_.h * 0.5f, 7.0f);
        fill();
        closePath();

        // MIDI channel button (click to cycle)
        const int ch = static_cast<int>(std::lround(values_[kParamMidiChannel]));
        std::snprintf(buf, sizeof(buf), "CH: %s", ch == 0 ? "All" : std::to_string(ch).c_str());
        chBtn_ = { W * 0.72f, kPad + 10.0f, 90.0f, 24.0f };
        beginPath();
        fillColor(37, 48, 58, 255);
        roundedRect(chBtn_.x, chBtn_.y, chBtn_.w, chBtn_.h, 6.0f);
        fill();
        closePath();
        beginPath();
        strokeColor(82, 112, 126, 180);
        strokeWidth(1.0f);
        roundedRect(chBtn_.x, chBtn_.y, chBtn_.w, chBtn_.h, 6.0f);
        stroke();
        closePath();
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(200, 215, 225, 255);
        text(chBtn_.x + chBtn_.w * 0.5f, chBtn_.y + chBtn_.h * 0.5f, buf, nullptr);
    }

    void drawZoneList(float W, float H)
    {
        const float listY = kHeaderH + kPad * 2.0f;
        const float listH = H - listY - kFooterH - kPad;

        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 232, 236, 255);
        text(kPad, listY - 18.0f, "Zones", nullptr);

        fontSize(11.0f);
        fillColor(108, 125, 137, 255);
        text(kPad + 56.0f, listY - 15.0f, "drag Root/Range to edit", nullptr);

        beginPath();
        fillColor(15, 23, 31, 212);
        roundedRect(kPad, listY, W - kPad * 2.0f, listH, 8.0f);
        fill();
        closePath();

        // Column headers
        const float hy = listY + 8.0f;
        fontSize(10.0f);
        fillColor(108, 125, 137, 255);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(kPad + kColNum,   hy, "#",     nullptr);
        text(kPad + kColRoot,  hy, "Root",  nullptr);
        text(kPad + kColRange, hy, "Range", nullptr);
        text(W - kPad - 116.0f, hy, "Loop", nullptr);
        text(kPad + kColFile,  hy, "File",  nullptr);

        beginPath();
        fillColor(50, 62, 72, 255);
        rect(kPad + 1.0f, listY + 22.0f, W - kPad * 2.0f - 2.0f, 1.0f);
        fill();
        closePath();

        const float rowsY = listY + 23.0f;
        for (std::size_t i = 0; i < zones_.size(); ++i) {
            const float ry  = rowsY + static_cast<float>(i) * kRowH;
            if (ry + kRowH > listY + listH) break;

            const ZoneEntry& z   = zones_[i];
            const bool even      = (i % 2) == 0;
            const bool isDragged = dragZoneIdx_ == static_cast<int>(i) && dragField_ != kDragNone;

            beginPath();
            fillColor(even ? 22 : 18, even ? 31 : 26,
                      isDragged ? 50 : (even ? 41 : 34), 255);
            rect(kPad + 1.0f, ry, W - kPad * 2.0f - 2.0f, kRowH - 1.0f);
            fill();
            closePath();

            const float mid = ry + kRowH * 0.5f;
            char buf[64];

            fontSize(11.0f);
            fillColor(200, 205, 215, 255);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);

            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(i + 1));
            text(kPad + kColNum, mid, buf, nullptr);

            // Root note (highlighted if being dragged)
            if (isDragged && dragField_ == kDragRoot)
                fillColor(240, 205, 170, 255);
            else
                fillColor(200, 205, 215, 255);
            text(kPad + kColRoot, mid, midiNoteName(z.rootNote).c_str(), nullptr);

            // Range (highlight low or high half based on drag)
            const std::string lo = midiNoteName(z.rangeLow);
            const std::string hi = midiNoteName(z.rangeHigh);
            if (isDragged && dragField_ == kDragRangeLow)
                fillColor(240, 205, 170, 255);
            else
                fillColor(200, 205, 215, 255);
            text(kPad + kColRange, mid, lo.c_str(), nullptr);

            fillColor(143, 158, 169, 255);
            text(kPad + kColRange + 36.0f, mid, "\xe2\x80\x93", nullptr);

            if (isDragged && dragField_ == kDragRangeHigh)
                fillColor(240, 205, 170, 255);
            else
                fillColor(200, 205, 215, 255);
            text(kPad + kColRange + 52.0f, mid, hi.c_str(), nullptr);

            // Loop button
            const Rect loopR { W - kPad - 76.0f, ry + 3.0f, 46.0f, 20.0f };
            beginPath();
            fillColor(z.loopEnabled ? 72 : 36,
                      z.loopEnabled ? 103 : 48,
                      z.loopEnabled ? 118 : 48, 255);
            roundedRect(loopR.x, loopR.y, loopR.w, loopR.h, 5.0f);
            fill();
            closePath();
            beginPath();
            strokeColor(z.loopEnabled ? 146 : 82,
                        z.loopEnabled ? 205 : 112,
                        z.loopEnabled ? 222 : 126,
                        z.loopEnabled ? 220 : 110);
            strokeWidth(1.0f);
            roundedRect(loopR.x, loopR.y, loopR.w, loopR.h, 5.0f);
            stroke();
            closePath();
            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(z.loopEnabled ? 246 : 178,
                      z.loopEnabled ? 250 : 191,
                      z.loopEnabled ? 252 : 200, 255);
            text(loopR.x + loopR.w * 0.5f, loopR.y + loopR.h * 0.5f,
                 z.loopEnabled ? "LOOP" : "OFF", nullptr);

            // Remove button
            const Rect removeR { W - kPad - 22.0f, ry + 4.0f, 18.0f, 18.0f };
            beginPath();
            fillColor(80, 36, 36, 255);
            roundedRect(removeR.x, removeR.y, removeR.w, removeR.h, 4.0f);
            fill();
            closePath();
            fontSize(11.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(200, 120, 120, 255);
            text(removeR.x + removeR.w * 0.5f, removeR.y + removeR.h * 0.5f,
                 "\xc3\x97", nullptr);

            // Filename
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(143, 158, 169, 255);
            text(kPad + kColFile, mid, basename(z.path).c_str(), nullptr);
        }

        if (zones_.empty()) {
            fontSize(13.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(80, 95, 108, 255);
            text(W * 0.5f, listY + listH * 0.5f,
                 "No zones loaded \xe2\x80\x94 click Load WAV or Record", nullptr);
        }
    }

    void drawFooter(float W, float H)
    {
        const float fy = H - kFooterH;

        beginPath();
        fillColor(50, 62, 72, 255);
        rect(kPad, fy, W - kPad * 2.0f, 1.0f);
        fill();
        closePath();

        loadBtn_ = { kPad, fy + 16.0f, 110.0f, 28.0f };
        drawButton(loadBtn_, "Load WAV", 51, 64, 74);

        recBtn_ = { kPad + 120.0f, fy + 16.0f, 100.0f, 28.0f };
        if (recording_) {
            drawButton(recBtn_, "\xe2\x97\x8f  Stop", 140, 40, 40);
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - recordStart_).count();
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%lld:%02lld",
                          static_cast<long long>(elapsed / 60),
                          static_cast<long long>(elapsed % 60));
            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(220, 100, 100, 255);
            text(kPad + 232.0f, fy + 30.0f, buf, nullptr);
        } else {
            drawButton(recBtn_, "\xe2\x97\x8f  Record", 80, 36, 36);
            char buf[48];
            std::snprintf(buf, sizeof(buf), "XFade: %.0f ms", values_[kParamCrossfadeDuration]);
            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(143, 158, 169, 255);
            text(kPad + 232.0f, fy + 30.0f, buf, nullptr);
        }

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d zone%s",
                      static_cast<int>(zones_.size()),
                      zones_.size() == 1 ? "" : "s");
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(108, 125, 137, 255);
        text(W - kPad, fy + 30.0f, buf, nullptr);
    }

    void drawButton(const Rect& r, const char* label, uint8_t cr, uint8_t cg, uint8_t cb)
    {
        beginPath();
        fillColor(cr, cg, cb, 255);
        roundedRect(r.x, r.y, r.w, r.h, 8.0f);
        fill();
        closePath();
        beginPath();
        strokeColor(142, 158, 168, 160);
        strokeWidth(1.0f);
        roundedRect(r.x, r.y, r.w, r.h, 8.0f);
        stroke();
        closePath();
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(239, 243, 246, 255);
        text(r.x + r.w * 0.5f, r.y + r.h * 0.5f + 1.0f, label, nullptr);
    }

    void rebuildFromZonesState(const std::string& s)
    {
        const auto metas = downspout::campione::deserializeZones(s);
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
    std::vector<ZoneEntry>             zones_;

    Rect loadBtn_  {};
    Rect recBtn_   {};
    Rect chBtn_    {};
    Rect volSlider_ {};

    DragField dragField_     = kDragNone;
    int       dragZoneIdx_   = -1;
    float     dragStartY_    = 0.0f;
    int       dragStartNote_ = 0;

    bool recording_ = false;
    std::chrono::steady_clock::time_point recordStart_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CampioneUI)
};

UI* createUI()
{
    return new CampioneUI();
}

END_NAMESPACE_DISTRHO
