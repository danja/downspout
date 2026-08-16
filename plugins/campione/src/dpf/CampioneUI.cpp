#include "DistrhoUI.hpp"

#include "campione_params.hpp"
#include "campione_sample_loader.hpp"
#include "campione_serialization.hpp"
#include "campione_ui_bridge.hpp"

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
using downspout::campione::kParamMcpEnabled;
using downspout::campione::kParamRecording;
using downspout::campione::kParamZonesVersion;
using downspout::campione::kParameterCount;
using downspout::campione::kStateKeyZoneFade;
using downspout::campione::kStateKeyZoneLoad;
using downspout::campione::kStateKeyZoneNormalize;
using downspout::campione::kStateKeyZoneRemove;
using downspout::campione::kStateKeyZoneReverse;
using downspout::campione::kStateKeyZoneTrim;
using downspout::campione::kStateKeyZoneUpdate;
using downspout::campione::kStateKeyParameters;
using downspout::campione::kStateKeyZones;
using downspout::campione::kStateKeyZoneDsp;

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct ZoneEntry {
    std::string  path;
    int          rootNote    = 60;
    int          rangeLow    = 0;
    int          rangeHigh   = 127;
    bool         loopEnabled = false;
    uint32_t     loopStart   = 0;
    uint32_t     loopEnd     = 0;
    uint32_t     totalFrames = 0;  // populated when waveform is loaded
    // ADSR
    float        attackMs     = 5.0f;
    float        decayMs      = 100.0f;
    float        sustainLevel = 1.0f;
    float        releaseMs    = 200.0f;
    // Filter
    bool         filterEnabled  = false;
    int          filterType     = 0;
    float        filterCutoffHz = 20000.0f;
    float        filterQ        = 0.707f;
};

enum DragField {
    kDragNone,
    kDragVol,
    kDragRoot,
    kDragRangeLow,
    kDragRangeHigh,
    kDragLoopStart,
    kDragLoopEnd,
    kDragAttack,
    kDragDecay,
    kDragSustain,
    kDragRelease,
    kDragFilterCutoff,
    kDragFilterQ
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

constexpr float kPad     = 20.0f;
constexpr float kHeaderH = 56.0f;
constexpr float kRowH    = 26.0f;
constexpr float kFooterH = 60.0f;
constexpr float kWaveH   = 170.0f;  // waveform 100px + 18px action bar + 52px ADSR/filter row

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
        values_[kParamMcpEnabled]        = 1.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index == kParamZonesVersion) {
            // DSP bumped the zones version — pull latest data from the bridge.
            std::string data;
            {
                std::lock_guard<std::mutex> lk(downspout::campione::uiBridge().zonesMtx);
                data = downspout::campione::uiBridge().zonesData;
            }
            lastZonesSerial_ = downspout::campione::uiBridge().zonesSerial.load(std::memory_order_acquire);
            rebuildFromZonesState(data);
            if (selectedZone_ >= static_cast<int>(zones_.size()))
                selectedZone_ = -1;
            repaint();
            return;
        }
        if (index < values_.size()) {
            values_[index] = value;
            repaint();
        }
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0) {
            const auto p = downspout::campione::deserializeParameters(value ? value : "");
            if (p.has_value()) {
                values_[kParamMasterVolume]      = p->masterVolume;
                values_[kParamMidiChannel]       = p->midiChannel;
                values_[kParamCrossfadeDuration] = p->crossfadeDurationMs;
                values_[kParamPitchBendRange]    = p->pitchBendRange;
                repaint();
            }
            return;
        }
        if (std::strcmp(key, kStateKeyZoneLoad) == 0 && value && value[0] != '\0') {
            ZoneEntry e;
            e.path = value;
            zones_.push_back(e);
            repaint();
            return;
        }
        if (std::strcmp(key, kStateKeyZones) == 0) {
            rebuildFromZonesState(value ? value : "");
            if (selectedZone_ >= static_cast<int>(zones_.size()))
                selectedZone_ = -1;
            repaint();
            return;
        }
        // Wave edit state keys are DSP-only commands; UI ignores them.
        // The DSP replies by updating kStateKeyZones which triggers rebuildFromZonesState.
        (void)value;
    }

    void uiIdle() override
    {
        // Poll the in-process bridge for MCP-driven zone changes.
        const uint32_t zSerial = downspout::campione::uiBridge().zonesSerial.load(std::memory_order_acquire);
        if (zSerial != lastZonesSerial_) {
            lastZonesSerial_ = zSerial;
            std::string data;
            {
                std::lock_guard<std::mutex> lk(downspout::campione::uiBridge().zonesMtx);
                data = downspout::campione::uiBridge().zonesData;
            }
            rebuildFromZonesState(data);
            if (selectedZone_ >= static_cast<int>(zones_.size()))
                selectedZone_ = -1;
            repaint();
        }

        // Poll for MCP-driven parameter changes.
        const uint32_t pSerial = downspout::campione::uiBridge().paramsSerial.load(std::memory_order_acquire);
        if (pSerial != lastParamsSerial_) {
            lastParamsSerial_ = pSerial;
            std::lock_guard<std::mutex> lk(downspout::campione::uiBridge().paramsMtx);
            auto& b = downspout::campione::uiBridge();
            values_[kParamMasterVolume]      = b.masterVolume;
            values_[kParamMidiChannel]       = b.midiChannel;
            values_[kParamCrossfadeDuration] = b.crossfadeMs;
            values_[kParamPitchBendRange]    = b.pitchBendRange;
            repaint();
        }

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
        drawWaveform(W, H);
        drawFooter(W, H);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;

        const float px = static_cast<float>(ev.pos.getX());
        const float py = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            // Commit loop handle drag
            if (dragField_ == kDragLoopStart || dragField_ == kDragLoopEnd) {
                if (selectedZone_ >= 0 && selectedZone_ < static_cast<int>(zones_.size()))
                    pushZoneUpdate(static_cast<std::size_t>(selectedZone_));
                dragField_ = kDragNone;
                return false;
            }
            // Commit ADSR/filter drag
            if ((dragField_ == kDragAttack || dragField_ == kDragDecay ||
                 dragField_ == kDragSustain || dragField_ == kDragRelease ||
                 dragField_ == kDragFilterCutoff || dragField_ == kDragFilterQ)
                && dragZoneIdx_ >= 0 && dragZoneIdx_ < static_cast<int>(zones_.size()))
            {
                pushZoneDspUpdate(static_cast<std::size_t>(dragZoneIdx_));
                dragField_   = kDragNone;
                dragZoneIdx_ = -1;
                return false;
            }
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

        // MCP toggle
        if (mcpBtn_.contains(px, py)) {
            const float newVal = values_[kParamMcpEnabled] >= 0.5f ? 0.0f : 1.0f;
            values_[kParamMcpEnabled] = newVal;
            editParameter(kParamMcpEnabled, true);
            setParameterValue(kParamMcpEnabled, newVal);
            editParameter(kParamMcpEnabled, false);
            repaint();
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

        // Loop handle drags (only meaningful when waveform is drawn)
        if (selectedZone_ >= 0 && selectedZone_ < static_cast<int>(zones_.size())
            && zones_[static_cast<std::size_t>(selectedZone_)].loopEnabled)
        {
            if (loopStartHandle_.contains(px, py)) {
                dragField_    = kDragLoopStart;
                dragStartX_   = px;
                dragStartFrame_ = zones_[static_cast<std::size_t>(selectedZone_)].loopStart;
                return true;
            }
            if (loopEndHandle_.contains(px, py)) {
                dragField_    = kDragLoopEnd;
                dragStartX_   = px;
                dragStartFrame_ = zones_[static_cast<std::size_t>(selectedZone_)].loopEnd;
                return true;
            }
        }

        // Wave edit action buttons
        if (selectedZone_ >= 0 && selectedZone_ < static_cast<int>(zones_.size())) {
            const int si = selectedZone_;
            char buf[64];
            if (waveNormBtn_.contains(px, py)) {
                std::snprintf(buf, sizeof(buf), "%d", si);
                setState(kStateKeyZoneNormalize, buf);
                return true;
            }
            if (waveTrimBtn_.contains(px, py)) {
                std::snprintf(buf, sizeof(buf), "%d|-60", si);
                setState(kStateKeyZoneTrim, buf);
                return true;
            }
            if (waveFadeBtn_.contains(px, py)) {
                std::snprintf(buf, sizeof(buf), "%d|10|10", si);
                setState(kStateKeyZoneFade, buf);
                return true;
            }
            if (waveRevBtn_.contains(px, py)) {
                std::snprintf(buf, sizeof(buf), "%d", si);
                setState(kStateKeyZoneReverse, buf);
                return true;
            }

            // Filter enable toggle
            if (filterEnableBtn_.contains(px, py)) {
                zones_[static_cast<std::size_t>(si)].filterEnabled =
                    !zones_[static_cast<std::size_t>(si)].filterEnabled;
                pushZoneDspUpdate(static_cast<std::size_t>(si));
                repaint();
                return true;
            }

            // Filter type toggle (cycles 0→1→2→3→0)
            if (filterTypeBtn_.contains(px, py)) {
                auto& fz = zones_[static_cast<std::size_t>(si)];
                fz.filterType = (fz.filterType + 1) % 4;
                pushZoneDspUpdate(static_cast<std::size_t>(si));
                repaint();
                return true;
            }

            // ADSR/filter drag slider start
            auto startDspDrag = [&](Rect& r, DragField field, float startVal) -> bool {
                if (r.contains(px, py)) {
                    dragField_     = field;
                    dragZoneIdx_   = si;
                    dragStartY_    = py;
                    dragStartFloat_ = startVal;
                    return true;
                }
                return false;
            };
            const ZoneEntry& dze = zones_[static_cast<std::size_t>(si)];
            if (startDspDrag(adsrAttackSlider_,   kDragAttack,       dze.attackMs))      return true;
            if (startDspDrag(adsrDecaySlider_,    kDragDecay,        dze.decayMs))       return true;
            if (startDspDrag(adsrSustainSlider_,  kDragSustain,      dze.sustainLevel))  return true;
            if (startDspDrag(adsrReleaseSlider_,  kDragRelease,      dze.releaseMs))     return true;
            if (startDspDrag(filterCutoffSlider_, kDragFilterCutoff, dze.filterCutoffHz)) return true;
            if (startDspDrag(filterQSlider_,      kDragFilterQ,      dze.filterQ))       return true;
        }

        // Zone row interactions
        const float W     = static_cast<float>(getWidth());
        const float H     = static_cast<float>(getHeight());
        const float listY = kHeaderH + kPad * 2.0f;
        const float listH = H - listY - kFooterH - kPad * 1.5f - kWaveH;
        const float rowsY = listY + 23.0f;

        for (std::size_t i = static_cast<std::size_t>(zoneScrollOffset_); i < zones_.size(); ++i) {
            const float ry = rowsY + static_cast<float>(i - static_cast<std::size_t>(zoneScrollOffset_)) * kRowH;
            if (ry > listY + listH) break;

            // Remove button
            const Rect removeR { W - kPad - 22.0f, ry + 4.0f, 18.0f, 18.0f };
            if (removeR.contains(px, py)) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(i));
                setState(kStateKeyZoneRemove, buf);
                zones_.erase(zones_.begin() + static_cast<std::ptrdiff_t>(i));
                if (selectedZone_ == static_cast<int>(i))
                    selectedZone_ = -1;
                else if (selectedZone_ > static_cast<int>(i))
                    --selectedZone_;
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

            // Range low/high — drag start
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

            // Row click (any unhandled area) → select zone and load waveform
            const Rect rowR { kPad, ry, W - kPad * 2.0f, kRowH };
            if (rowR.contains(px, py)) {
                if (selectedZone_ != static_cast<int>(i)) {
                    selectedZone_ = static_cast<int>(i);
                    loadPeaks(selectedZone_);
                    repaint();
                }
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

        if (dragField_ == kDragLoopStart || dragField_ == kDragLoopEnd) {
            if (selectedZone_ >= 0 && selectedZone_ < static_cast<int>(zones_.size())
                && waveRect_.w > 0.0f)
            {
                ZoneEntry& z = zones_[static_cast<std::size_t>(selectedZone_)];
                if (z.totalFrames > 0) {
                    const float dx    = px - dragStartX_;
                    const int delta   = static_cast<int>(dx / waveRect_.w
                                                         * static_cast<float>(z.totalFrames));
                    const int newF    = std::clamp(static_cast<int>(dragStartFrame_) + delta,
                                                   0, static_cast<int>(z.totalFrames) - 1);
                    if (dragField_ == kDragLoopStart)
                        z.loopStart = static_cast<uint32_t>(std::min(newF, static_cast<int>(z.loopEnd)));
                    else
                        z.loopEnd = static_cast<uint32_t>(std::max(newF, static_cast<int>(z.loopStart)));
                    repaint();
                    return true;
                }
            }
            return true;
        }

        if ((dragField_ == kDragAttack || dragField_ == kDragDecay ||
             dragField_ == kDragSustain || dragField_ == kDragRelease ||
             dragField_ == kDragFilterCutoff || dragField_ == kDragFilterQ)
            && dragZoneIdx_ >= 0 && dragZoneIdx_ < static_cast<int>(zones_.size()))
        {
            ZoneEntry& dz = zones_[static_cast<std::size_t>(dragZoneIdx_)];
            const float dy = dragStartY_ - py;  // upward = increase
            if (dragField_ == kDragAttack) {
                dz.attackMs = std::clamp(dragStartFloat_ + dy * 2.0f, 0.0f, 5000.0f);
            } else if (dragField_ == kDragDecay) {
                dz.decayMs = std::clamp(dragStartFloat_ + dy * 2.0f, 0.0f, 5000.0f);
            } else if (dragField_ == kDragSustain) {
                dz.sustainLevel = std::clamp(dragStartFloat_ + dy / 100.0f, 0.0f, 1.0f);
            } else if (dragField_ == kDragRelease) {
                dz.releaseMs = std::clamp(dragStartFloat_ + dy * 2.0f, 0.0f, 10000.0f);
            } else if (dragField_ == kDragFilterCutoff) {
                // Logarithmic feel: scale by factor relative to start value
                const float factor = std::pow(2.0f, dy / 60.0f);
                dz.filterCutoffHz = std::clamp(dragStartFloat_ * factor, 20.0f, 20000.0f);
            } else if (dragField_ == kDragFilterQ) {
                dz.filterQ = std::clamp(dragStartFloat_ + dy / 100.0f, 0.1f, 20.0f);
            }
            repaint();
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

    bool onScroll(const ScrollEvent& ev) override
    {
        const float H     = static_cast<float>(getHeight());
        const float listY = kHeaderH + kPad * 2.0f;
        const float listH = H - listY - kFooterH - kPad * 1.5f - kWaveH;
        const float py    = static_cast<float>(ev.pos.getY());
        if (py < listY || py > listY + listH) return false;

        const int maxScroll = std::max(0, static_cast<int>(zones_.size()) - static_cast<int>(listH / kRowH));
        zoneScrollOffset_ = std::clamp(zoneScrollOffset_ - static_cast<int>(ev.delta.getY()), 0, maxScroll);
        repaint();
        return true;
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
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%d|%d|%d|%d|%d|%u|%u",
                      static_cast<int>(idx),
                      z.rootNote, z.rangeLow, z.rangeHigh,
                      z.loopEnabled ? 1 : 0,
                      z.loopStart, z.loopEnd);
        setState(kStateKeyZoneUpdate, buf);
    }

    void pushZoneDspUpdate(std::size_t idx)
    {
        if (idx >= zones_.size()) return;
        const ZoneEntry& z = zones_[idx];
        char buf[192];
        std::snprintf(buf, sizeof(buf), "%d|%.4f|%.4f|%.4f|%.4f|%d|%d|%.2f|%.4f",
                      static_cast<int>(idx),
                      z.attackMs, z.decayMs, z.sustainLevel, z.releaseMs,
                      z.filterEnabled ? 1 : 0, z.filterType,
                      z.filterCutoffHz, z.filterQ);
        setState(kStateKeyZoneDsp, buf);
    }

    void loadPeaks(int idx)
    {
        peaks_.clear();
        peakZoneIdx_ = -1;
        if (idx < 0 || idx >= static_cast<int>(zones_.size())) return;

        ZoneEntry& z = zones_[static_cast<std::size_t>(idx)];
        if (z.path.empty()) return;

        auto result = downspout::campione::loadWavZone(z.path);
        if (!result.error.empty() || result.zone.data.empty()) return;

        const auto& data   = result.zone.data;
        const int chCount  = result.zone.channelCount;
        const int totalF   = static_cast<int>(data.size()) / std::max(1, chCount);

        z.totalFrames = static_cast<uint32_t>(totalF);

        constexpr int kPeakBins = 400;
        peaks_.resize(kPeakBins * 2, 0.0f);

        for (int bin = 0; bin < kPeakBins; ++bin) {
            const int f0 = static_cast<int>(static_cast<int64_t>(bin)     * totalF / kPeakBins);
            const int f1 = static_cast<int>(static_cast<int64_t>(bin + 1) * totalF / kPeakBins);
            float mn = 0.0f, mx = 0.0f;
            for (int f = f0; f < f1; ++f) {
                float s = 0.0f;
                for (int c = 0; c < chCount; ++c)
                    s += data[static_cast<std::size_t>(f * chCount + c)];
                s /= static_cast<float>(std::max(1, chCount));
                mn = std::min(mn, s);
                mx = std::max(mx, s);
            }
            peaks_[bin * 2 + 0] = mn;
            peaks_[bin * 2 + 1] = mx;
        }
        peakZoneIdx_ = idx;
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
        const float listH = H - listY - kFooterH - kPad * 1.5f - kWaveH;

        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 232, 236, 255);
        text(kPad, listY - 18.0f, "Zones", nullptr);

        fontSize(11.0f);
        fillColor(108, 125, 137, 255);
        text(kPad + 56.0f, listY - 15.0f, "drag Root/Range to edit  \xe2\x80\xa2  click row to view waveform", nullptr);

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
        for (std::size_t i = static_cast<std::size_t>(zoneScrollOffset_); i < zones_.size(); ++i) {
            const float ry  = rowsY + static_cast<float>(i - static_cast<std::size_t>(zoneScrollOffset_)) * kRowH;
            if (ry + kRowH > listY + listH) break;

            const ZoneEntry& z      = zones_[i];
            const bool even         = (i % 2) == 0;
            const bool isDragged    = dragZoneIdx_ == static_cast<int>(i) && dragField_ != kDragNone;
            const bool isSelected   = selectedZone_ == static_cast<int>(i);

            beginPath();
            if (isSelected)
                fillColor(28, 42, 60, 255);
            else
                fillColor(even ? 22 : 18, even ? 31 : 26,
                          isDragged ? 50 : (even ? 41 : 34), 255);
            rect(kPad + 1.0f, ry, W - kPad * 2.0f - 2.0f, kRowH - 1.0f);
            fill();
            closePath();

            // Selection accent bar
            if (isSelected) {
                beginPath();
                fillColor(82, 142, 200, 255);
                rect(kPad + 1.0f, ry, 3.0f, kRowH - 1.0f);
                fill();
                closePath();
            }

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

            // Range
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

    void drawWaveform(float W, float H)
    {
        const float listY  = kHeaderH + kPad * 2.0f;
        const float listH  = H - listY - kFooterH - kPad * 1.5f - kWaveH;
        const float waveY  = listY + listH + kPad * 0.5f;
        const float waveW  = W - kPad * 2.0f;

        waveRect_ = { kPad, waveY, waveW, kWaveH };

        // Background
        beginPath();
        fillColor(10, 18, 26, 255);
        roundedRect(waveRect_.x, waveRect_.y, waveRect_.w, waveRect_.h, 6.0f);
        fill();
        closePath();

        if (selectedZone_ < 0 || selectedZone_ >= static_cast<int>(zones_.size())) {
            // No zone selected
            fontSize(11.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(55, 75, 90, 255);
            text(waveRect_.x + waveRect_.w * 0.5f, waveRect_.y + waveRect_.h * 0.5f,
                 "click a zone row to view its waveform", nullptr);

            beginPath();
            strokeColor(30, 44, 56, 255);
            strokeWidth(1.0f);
            roundedRect(waveRect_.x, waveRect_.y, waveRect_.w, waveRect_.h, 6.0f);
            stroke();
            closePath();
            loopStartHandle_ = {};
            loopEndHandle_   = {};
            return;
        }

        const ZoneEntry& z = zones_[static_cast<std::size_t>(selectedZone_)];
        const uint32_t totalF = z.totalFrames > 0 ? z.totalFrames : 1;

        // Loop region highlight (confined to waveform display area)
        constexpr float kWaveDisplayH = 100.0f;
        if (z.loopEnabled && z.loopEnd > z.loopStart) {
            const float lx0 = waveRect_.x + static_cast<float>(z.loopStart) / static_cast<float>(totalF) * waveRect_.w;
            const float lx1 = waveRect_.x + static_cast<float>(z.loopEnd)   / static_cast<float>(totalF) * waveRect_.w;
            beginPath();
            fillColor(36, 68, 88, 100);
            rect(lx0, waveRect_.y, lx1 - lx0, kWaveDisplayH);
            fill();
            closePath();
        }

        // Waveform peaks (confined to top 100px; action bar + ADSR row occupy the rest)
        if (!peaks_.empty() && peakZoneIdx_ == selectedZone_) {
            const int kPeakBins = static_cast<int>(peaks_.size()) / 2;
            const float midY = waveRect_.y + kWaveDisplayH * 0.5f;
            const float ampH = kWaveDisplayH * 0.44f;

            for (int bin = 0; bin < kPeakBins; ++bin) {
                const float fx  = waveRect_.x + static_cast<float>(bin)     / static_cast<float>(kPeakBins) * waveRect_.w;
                const float fw  = waveRect_.w / static_cast<float>(kPeakBins);
                const float mn  = peaks_[bin * 2 + 0];
                const float mx  = peaks_[bin * 2 + 1];
                const float top = midY + mn * ampH;
                const float ht  = std::max(1.0f, (mx - mn) * ampH);

                beginPath();
                fillColor(72, 140, 180, 200);
                rect(fx, top, std::max(1.0f, fw - 0.5f), ht);
                fill();
                closePath();
            }
        } else if (z.totalFrames == 0 && !z.path.empty()) {
            fontSize(11.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(72, 95, 112, 255);
            text(waveRect_.x + waveRect_.w * 0.5f, waveRect_.y + kWaveDisplayH * 0.5f,
                 "loading\xe2\x80\xa6", nullptr);
        }

        // Loop handles (only when loop enabled and waveform loaded)
        if (z.loopEnabled && z.totalFrames > 0) {
            const float lx0 = waveRect_.x + static_cast<float>(z.loopStart) / static_cast<float>(totalF) * waveRect_.w;
            const float lx1 = waveRect_.x + static_cast<float>(z.loopEnd)   / static_cast<float>(totalF) * waveRect_.w;

            // Loop start — green line + tab (confined to waveform display area)
            beginPath();
            fillColor(60, 190, 110, 200);
            rect(lx0, waveRect_.y, 2.0f, kWaveDisplayH);
            fill();
            closePath();
            beginPath();
            fillColor(60, 200, 110, 255);
            roundedRect(lx0 - 5.0f, waveRect_.y, 12.0f, 12.0f, 3.0f);
            fill();
            closePath();
            loopStartHandle_ = { lx0 - 6.0f, waveRect_.y, 14.0f, kWaveDisplayH };

            // Loop end — orange line + tab
            beginPath();
            fillColor(210, 130, 50, 200);
            rect(lx1 - 2.0f, waveRect_.y, 2.0f, kWaveDisplayH);
            fill();
            closePath();
            beginPath();
            fillColor(220, 140, 55, 255);
            roundedRect(lx1 - 7.0f, waveRect_.y, 12.0f, 12.0f, 3.0f);
            fill();
            closePath();
            loopEndHandle_ = { lx1 - 8.0f, waveRect_.y, 14.0f, kWaveDisplayH };
        } else {
            loopStartHandle_ = {};
            loopEndHandle_   = {};
        }

        // Filename label top-left
        if (!z.path.empty()) {
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(72, 95, 110, 220);
            text(waveRect_.x + 6.0f, waveRect_.y + 4.0f, basename(z.path).c_str(), nullptr);
        }

        // Action bar (Normalize / Trim / Fade / Reverse)
        {
            constexpr float kActionH = 16.0f;
            // Place action bar above the ADSR row, leaving 52px at bottom for ADSR/filter
            const float ay = waveRect_.y + waveRect_.h - 52.0f - kActionH - 2.0f;
            const float btnW = 66.0f;
            const float gap  = 6.0f;
            float bx = waveRect_.x + 6.0f;

            auto drawActionBtn = [&](Rect& out, const char* label) {
                out = { bx, ay, btnW, kActionH };
                beginPath();
                fillColor(28, 44, 56, 240);
                roundedRect(out.x, out.y, out.w, out.h, 4.0f);
                fill();
                closePath();
                beginPath();
                strokeColor(60, 88, 108, 180);
                strokeWidth(1.0f);
                roundedRect(out.x, out.y, out.w, out.h, 4.0f);
                stroke();
                closePath();
                fontSize(9.0f);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fillColor(180, 200, 215, 255);
                text(out.x + out.w * 0.5f, out.y + out.h * 0.5f, label, nullptr);
                bx += btnW + gap;
            };

            drawActionBtn(waveNormBtn_, "Normalize");
            drawActionBtn(waveTrimBtn_, "Trim");
            drawActionBtn(waveFadeBtn_, "Fade");
            drawActionBtn(waveRevBtn_,  "Reverse");
        }

        // ADSR + filter row (bottom 50px of waveform panel)
        if (selectedZone_ >= 0 && selectedZone_ < static_cast<int>(zones_.size()))
        {
            const ZoneEntry& dz = zones_[static_cast<std::size_t>(selectedZone_)];
            const float ry  = waveRect_.y + waveRect_.h - 50.0f;

            // Separator line
            beginPath();
            fillColor(30, 44, 56, 200);
            rect(waveRect_.x + 2.0f, ry, waveRect_.w - 4.0f, 1.0f);
            fill();
            closePath();

            // Helper: draw a labeled drag slider
            auto drawDragSlider = [&](Rect& out, float bx, float by, float bw,
                                      const char* label, const char* valStr, bool active) {
                out = { bx, by, bw, 38.0f };
                // Label
                fontSize(8.5f);
                textAlign(ALIGN_CENTER | ALIGN_TOP);
                fillColor(108, 130, 148, 255);
                text(bx + bw * 0.5f, by + 2.0f, label, nullptr);
                // Value box
                const float vby = by + 13.0f;
                beginPath();
                fillColor(active ? 40 : 22, active ? 60 : 34, active ? 80 : 44, 255);
                roundedRect(bx + 2.0f, vby, bw - 4.0f, 20.0f, 3.0f);
                fill();
                closePath();
                beginPath();
                strokeColor(active ? 100 : 55, active ? 150 : 78, active ? 180 : 95, 180);
                strokeWidth(1.0f);
                roundedRect(bx + 2.0f, vby, bw - 4.0f, 20.0f, 3.0f);
                stroke();
                closePath();
                fontSize(9.0f);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fillColor(active ? 240 : 180, active ? 210 : 200, active ? 170 : 215, 255);
                text(bx + bw * 0.5f, vby + 10.0f, valStr, nullptr);
            };

            char buf[32];
            const float slotW = (waveRect_.w - 12.0f) / 8.0f;
            float sx = waveRect_.x + 6.0f;

            // Attack
            std::snprintf(buf, sizeof(buf), "%.0fms", dz.attackMs);
            drawDragSlider(adsrAttackSlider_,  sx, ry + 2.0f, slotW - 2.0f, "ATK", buf,
                           dragField_ == kDragAttack && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Decay
            std::snprintf(buf, sizeof(buf), "%.0fms", dz.decayMs);
            drawDragSlider(adsrDecaySlider_,   sx, ry + 2.0f, slotW - 2.0f, "DEC", buf,
                           dragField_ == kDragDecay && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Sustain
            std::snprintf(buf, sizeof(buf), "%.2f", dz.sustainLevel);
            drawDragSlider(adsrSustainSlider_, sx, ry + 2.0f, slotW - 2.0f, "SUS", buf,
                           dragField_ == kDragSustain && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Release
            std::snprintf(buf, sizeof(buf), "%.0fms", dz.releaseMs);
            drawDragSlider(adsrReleaseSlider_, sx, ry + 2.0f, slotW - 2.0f, "REL", buf,
                           dragField_ == kDragRelease && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Filter enable toggle
            filterEnableBtn_ = { sx + 1.0f, ry + 4.0f, slotW - 4.0f, 36.0f };
            beginPath();
            fillColor(dz.filterEnabled ? 36 : 22, dz.filterEnabled ? 70 : 34, dz.filterEnabled ? 90 : 44, 255);
            roundedRect(filterEnableBtn_.x, filterEnableBtn_.y, filterEnableBtn_.w, filterEnableBtn_.h, 4.0f);
            fill();
            closePath();
            beginPath();
            strokeColor(dz.filterEnabled ? 80 : 50, dz.filterEnabled ? 160 : 78, dz.filterEnabled ? 190 : 95, 180);
            strokeWidth(1.0f);
            roundedRect(filterEnableBtn_.x, filterEnableBtn_.y, filterEnableBtn_.w, filterEnableBtn_.h, 4.0f);
            stroke();
            closePath();
            fontSize(8.5f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(dz.filterEnabled ? 200 : 140, dz.filterEnabled ? 230 : 170, dz.filterEnabled ? 245 : 185, 255);
            text(filterEnableBtn_.x + filterEnableBtn_.w * 0.5f, filterEnableBtn_.y + filterEnableBtn_.h * 0.5f - 6.0f, "FLT", nullptr);
            fillColor(dz.filterEnabled ? 120 : 100, dz.filterEnabled ? 220 : 140, dz.filterEnabled ? 240 : 160, 255);
            text(filterEnableBtn_.x + filterEnableBtn_.w * 0.5f, filterEnableBtn_.y + filterEnableBtn_.h * 0.5f + 6.0f,
                 dz.filterEnabled ? "ON" : "OFF", nullptr);
            sx += slotW;

            // Filter type toggle
            static const char* kFltNames[] = {"LP","BP","HP","NT"};
            filterTypeBtn_ = { sx + 1.0f, ry + 4.0f, slotW - 4.0f, 36.0f };
            beginPath();
            fillColor(28, 44, 58, 255);
            roundedRect(filterTypeBtn_.x, filterTypeBtn_.y, filterTypeBtn_.w, filterTypeBtn_.h, 4.0f);
            fill();
            closePath();
            beginPath();
            strokeColor(55, 80, 100, 180);
            strokeWidth(1.0f);
            roundedRect(filterTypeBtn_.x, filterTypeBtn_.y, filterTypeBtn_.w, filterTypeBtn_.h, 4.0f);
            stroke();
            closePath();
            fontSize(8.5f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(108, 130, 148, 255);
            text(filterTypeBtn_.x + filterTypeBtn_.w * 0.5f, filterTypeBtn_.y + filterTypeBtn_.h * 0.5f - 6.0f, "TYPE", nullptr);
            fillColor(200, 215, 225, 255);
            text(filterTypeBtn_.x + filterTypeBtn_.w * 0.5f, filterTypeBtn_.y + filterTypeBtn_.h * 0.5f + 6.0f,
                 kFltNames[std::clamp(dz.filterType, 0, 3)], nullptr);
            sx += slotW;

            // Cutoff
            std::snprintf(buf, sizeof(buf), "%.0fHz", dz.filterCutoffHz);
            drawDragSlider(filterCutoffSlider_, sx, ry + 2.0f, slotW - 2.0f, "CUTOFF", buf,
                           dragField_ == kDragFilterCutoff && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Q
            std::snprintf(buf, sizeof(buf), "%.2f", dz.filterQ);
            drawDragSlider(filterQSlider_, sx, ry + 2.0f, slotW - 2.0f, "Q", buf,
                           dragField_ == kDragFilterQ && dragZoneIdx_ == selectedZone_);
        }

        // Border
        beginPath();
        strokeColor(32, 48, 62, 255);
        strokeWidth(1.0f);
        roundedRect(waveRect_.x, waveRect_.y, waveRect_.w, waveRect_.h, 6.0f);
        stroke();
        closePath();
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

        // MCP toggle button
        mcpBtn_ = { W - kPad - 72.0f, fy + 16.0f, 72.0f, 28.0f };
        const bool mcpOn = values_[kParamMcpEnabled] >= 0.5f;
        drawButton(mcpBtn_, mcpOn ? "MCP ON" : "MCP OFF",
                   mcpOn ? 30 : 51, mcpOn ? 100 : 64, mcpOn ? 30 : 74);

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
        zoneScrollOffset_ = 0;
        zones_.reserve(metas->size());
        for (const auto& z : *metas) {
            ZoneEntry e;
            e.path          = z.sourcePath;
            e.rootNote      = z.rootNote;
            e.rangeLow      = z.rangeLow;
            e.rangeHigh     = z.rangeHigh;
            e.loopEnabled   = z.loopEnabled;
            e.loopStart     = z.loopStart;
            e.loopEnd       = z.loopEnd;
            e.attackMs      = z.attackMs;
            e.decayMs       = z.decayMs;
            e.sustainLevel  = z.sustainLevel;
            e.releaseMs     = z.releaseMs;
            e.filterEnabled = z.filterEnabled;
            e.filterType    = z.filterType;
            e.filterCutoffHz = z.filterCutoffHz;
            e.filterQ       = z.filterQ;
            zones_.push_back(std::move(e));
        }
        // Reload waveform peaks if selected zone is still valid
        if (selectedZone_ >= 0 && selectedZone_ < static_cast<int>(zones_.size()))
            loadPeaks(selectedZone_);
    }

    std::array<float, kParameterCount> values_ {};
    std::vector<ZoneEntry>             zones_;

    Rect loadBtn_   {};
    Rect recBtn_    {};
    Rect mcpBtn_    {};
    Rect chBtn_     {};
    Rect volSlider_ {};
    Rect waveRect_       {};
    Rect loopStartHandle_{};
    Rect loopEndHandle_  {};
    // Wave edit action buttons (built during draw, hit-tested in onMouse)
    Rect waveNormBtn_  {};
    Rect waveTrimBtn_  {};
    Rect waveFadeBtn_  {};
    Rect waveRevBtn_   {};
    // ADSR/filter drag sliders (built during draw, hit-tested in onMouse)
    Rect adsrAttackSlider_  {};
    Rect adsrDecaySlider_   {};
    Rect adsrSustainSlider_ {};
    Rect adsrReleaseSlider_ {};
    Rect filterEnableBtn_   {};
    Rect filterTypeBtn_     {};
    Rect filterCutoffSlider_{};
    Rect filterQSlider_     {};
    // For ADSR float drags: track start value during drag
    float dragStartFloat_ = 0.0f;

    DragField dragField_     = kDragNone;
    int       dragZoneIdx_   = -1;
    float     dragStartY_    = 0.0f;
    float     dragStartX_    = 0.0f;
    int       dragStartNote_ = 0;
    uint32_t  dragStartFrame_= 0;

    int              selectedZone_ = -1;
    std::vector<float> peaks_;
    int              peakZoneIdx_  = -1;

    bool recording_ = false;
    std::chrono::steady_clock::time_point recordStart_;

    uint32_t lastZonesSerial_  = 0;
    uint32_t lastParamsSerial_ = 0;
    int      zoneScrollOffset_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CampioneUI)
};

UI* createUI()
{
    return new CampioneUI();
}

END_NAMESPACE_DISTRHO
