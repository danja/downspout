#include "DistrhoUI.hpp"

#include "campione_drum_map.hpp"
#include "campione_params.hpp"
#include "campione_sample_loader.hpp"
#include "campione_serialization.hpp"
#include "campione_ui_bridge.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
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
using downspout::campione::kStateKeyZoneSlice;
using downspout::campione::kStateKeyZoneReorder;
using downspout::campione::kStateKeyPatchSave;
using downspout::campione::kStateKeyPatchLoad;
using downspout::campione::kStateKeyDataDir;
using downspout::campione::kStateKeyZonePreview;
using downspout::campione::kStateKeyWavetableImport;
using downspout::campione::kStateKeyZoneClear;
using downspout::campione::kStateKeyMapDrum;
using downspout::campione::kStateKeyZoneShuffle;
using downspout::campione::kDefaultDataDir;

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
    float        pan            = 0.0f;
    bool         muted          = false;
    int          octaveShift    = 0;
};

enum DragField {
    kDragNone,
    kDragVol,
    kDragZoneReorder,   // drag zone row up/down to reorder
    kDragRoot,
    kDragOctave,
    kDragRangeLow,
    kDragRangeHigh,
    kDragLoopStart,
    kDragLoopEnd,
    kDragAttack,
    kDragDecay,
    kDragSustain,
    kDragRelease,
    kDragFilterCutoff,
    kDragFilterQ,
    kDragPan
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

enum DialogMode { kDialogNone, kDialogSavePatch, kDialogSettings, kDialogContextMenu, kDialogDrumReport };

static std::string uiDefaultDataDir()
{
    const char* h = std::getenv("HOME");
    return std::string(h ? h : "/tmp") + kDefaultDataDir;
}

// Column x positions (left edge relative to kPad)
constexpr float kColNum    =  8.0f;
constexpr float kColRoot   = 28.0f;
constexpr float kColOctave = 82.0f;
constexpr float kColRange  = 134.0f;
constexpr float kColFile   = 246.0f;

struct PeakResult {
    std::vector<float> peaks;
    int      zoneIdx     = -1;
    uint32_t totalFrames = 0;
    bool     failed      = false;
};

}  // namespace

class CampioneUI : public UI
{
public:
    CampioneUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
        , dataDir_(uiDefaultDataDir())
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
        // setState must not be called from the constructor: the VST3 component
        // handler may not be set up yet, causing the host to crash.  Deferred
        // to the first uiIdle() call via dataDirSent_.
    }

    ~CampioneUI() override
    {
        peakPendingIdx_ = -1;
        if (peakThread_.joinable())     peakThread_.join();
        if (filePickerThread_.joinable()) filePickerThread_.join();
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
            if (pendingDrumDialog_) {
                pendingDrumDialog_ = false;
                drumReportScrollOffset_ = 0;
                drumReportAssignments_.resize(zones_.size());
                for (std::size_t i = 0; i < zones_.size(); ++i) {
                    const int oldNote = (i < preDrumMapZones_.size())
                                        ? preDrumMapZones_[i].rootNote : -1;
                    const int newNote = zones_[i].rootNote;
                    auto& a = drumReportAssignments_[i];
                    if (newNote != oldNote && newNote >= 0) {
                        a.gmNote     = newNote;
                        a.confidence = 1.0f;
                        a.evidence   = downspout::campione::gmNoteName(newNote);
                    } else {
                        a = {};  // gmNote = -1: unassigned / unchanged
                    }
                }
                dialogMode_ = kDialogDrumReport;
            }
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
            // If the bridge has been populated, it reflects the actual DSP state
            // (set by notifyZonesChanged). Prefer it over the raw host-pushed value,
            // which may be stale (REAPER resends the saved project state each time
            // the plugin window opens, even if zones changed since the last save).
            const uint32_t bridgeSerial =
                downspout::campione::uiBridge().zonesSerial.load(std::memory_order_acquire);
            if (bridgeSerial > 0) {
                std::string data;
                {
                    std::lock_guard<std::mutex> lk(downspout::campione::uiBridge().zonesMtx);
                    data = downspout::campione::uiBridge().zonesData;
                }
                lastZonesSerial_ = bridgeSerial;
                rebuildFromZonesState(data);
            } else {
                rebuildFromZonesState(value ? value : "");
            }
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
        // Send data directory to DSP on first idle (safe point after host init).
        if (!dataDirSent_) {
            dataDirSent_ = true;
            setState(kStateKeyDataDir, dataDir_.c_str());
        }

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

        // Apply completed async peak load, then start next queued load if any.
        if (peakThreadDone_.load(std::memory_order_acquire)) {
            if (peakThread_.joinable()) {
                peakThread_.join();
                PeakResult r;
                { std::lock_guard<std::mutex> lk(peakResultMtx_); r = std::move(peakThreadResult_); }
                if (!r.failed && r.zoneIdx >= 0 && r.zoneIdx < static_cast<int>(zones_.size())) {
                    peaks_          = std::move(r.peaks);
                    peakZoneIdx_    = r.zoneIdx;
                    peakLoadFailed_ = false;
                    zones_[static_cast<std::size_t>(r.zoneIdx)].totalFrames = r.totalFrames;
                } else {
                    peakLoadFailed_ = (r.zoneIdx >= 0);
                }
                repaint();
            }
            if (peakPendingIdx_ >= 0) {
                const int idx = std::exchange(peakPendingIdx_, -1);
                if (idx < static_cast<int>(zones_.size())
                        && !zones_[static_cast<std::size_t>(idx)].path.empty())
                    launchPeakThread(idx, zones_[static_cast<std::size_t>(idx)].path);
            }
        }

        // Apply completed multi-file picker results.
        if (filePickerDone_.load(std::memory_order_acquire) && filePickerThread_.joinable()) {
            filePickerThread_.join();
            std::vector<std::string> paths;
            bool fallback = false;
            {
                std::lock_guard<std::mutex> lk(filePickerMtx_);
                paths    = std::move(filePickerPaths_);
                fallback = filePickerFallback_;
            }
            const bool isImport = filePickerIsImport_;
            filePickerIsImport_ = false;
            const char* stateKey = isImport ? kStateKeyWavetableImport : kStateKeyZoneLoad;
            if (fallback) {
                requestStateFile(stateKey);
                showStatus("Loading file…");
            } else if (!paths.empty()) {
                char sb[48];
                std::snprintf(sb, sizeof(sb), "Loading %d file%s…",
                              static_cast<int>(paths.size()), paths.size() == 1 ? "" : "s");
                showStatus(sb);
                for (const auto& p : paths)
                    setState(stateKey, p.c_str());
            } else {
                showStatus("No files selected");
            }
        }

        // Rate-limit animation repaints (recording timer, status flash, dialog, btn flash).
        const bool btnFlashing = (flashedBtn_ != kFlashNone &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - flashedBtnAt_).count() < 200);
        if (recording_ || !statusMsg_.empty() || dialogMode_ != kDialogNone || btnFlashing) {
            using Clock = std::chrono::steady_clock;
            const auto now = Clock::now();
            const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - lastAnimRepaint_).count();
            if (ms >= 50) {
                lastAnimRepaint_ = now;
                repaint();
            }
        }
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
        if (dialogMode_ == kDialogContextMenu)
            drawContextMenu(W, H);
        else if (dialogMode_ == kDialogDrumReport)
            drawDrumReportDialog(W, H);
        else if (dialogMode_ != kDialogNone)
            drawDialog(W, H);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 && ev.button != 2) return false;

        const float px = static_cast<float>(ev.pos.getX());
        const float py = static_cast<float>(ev.pos.getY());

        // Right-click anywhere in the zone list area → open context menu.
        if (ev.button == 2 && ev.press) {
            if (dialogMode_ == kDialogContextMenu) {
                dialogMode_ = kDialogNone;
            } else {
                const float listY = kHeaderH + kPad * 2.0f;
                const float W     = static_cast<float>(getWidth());
                const float H     = static_cast<float>(getHeight());
                const float listH = H - listY - kFooterH - kPad * 1.5f - kWaveH;
                if (px >= kPad && px <= W - kPad && py >= listY && py <= listY + listH) {
                    contextMenuX_ = px;
                    contextMenuY_ = py;
                    dialogMode_   = kDialogContextMenu;
                }
            }
            repaint();
            return true;
        }

        if (ev.button != 1) return false;

        // When a modal dialog is open, route all left-clicks to it.
        if (dialogMode_ == kDialogContextMenu) {
            if (!ev.press) return true;
            dispatchContextMenuClick(px, py);
            return true;
        }
        if (dialogMode_ != kDialogNone) {
            if (!ev.press) return false;
            if (dialogOkBtn_.contains(px, py))     { confirmDialog(); return true; }
            if (dialogCancelBtn_.contains(px, py)) {
                if (dialogMode_ == kDialogDrumReport) {
                    // DSP already applied; revert by pushing back the pre-map zones.
                    for (std::size_t i = 0; i < preDrumMapZones_.size() && i < zones_.size(); ++i) {
                        zones_[i].rootNote  = preDrumMapZones_[i].rootNote;
                        zones_[i].rangeLow  = preDrumMapZones_[i].rangeLow;
                        zones_[i].rangeHigh = preDrumMapZones_[i].rangeHigh;
                        pushZoneUpdate(static_cast<int>(i));
                    }
                    preDrumMapZones_.clear();
                    drumReportAssignments_.clear();
                }
                dialogMode_ = kDialogNone; repaint(); return true;
            }
            getWindow().focus();  // re-assert keyboard focus on any click inside dialog
            return true; // swallow all other clicks
        }

        if (!ev.press) {
            // Commit zone reorder drag
            if (dragField_ == kDragZoneReorder) {
                const int from = reorderFromIdx_;
                const int to   = reorderTargetIdx_;
                dragField_        = kDragNone;
                reorderFromIdx_   = -1;
                reorderTargetIdx_ = -1;
                if (from >= 0 && to >= 0 && to != from && to != from + 1) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%d|%d", from, to);
                    setState(kStateKeyZoneReorder, buf);
                }
                repaint();
                return true;
            }
            // Commit loop handle drag
            if (dragField_ == kDragLoopStart || dragField_ == kDragLoopEnd) {
                if (selectedZone_ >= 0 && selectedZone_ < static_cast<int>(zones_.size()))
                    pushZoneUpdate(static_cast<std::size_t>(selectedZone_));
                dragField_ = kDragNone;
                return false;
            }
            // Commit ADSR/filter/pan drag
            if ((dragField_ == kDragAttack || dragField_ == kDragDecay ||
                 dragField_ == kDragSustain || dragField_ == kDragRelease ||
                 dragField_ == kDragFilterCutoff || dragField_ == kDragFilterQ ||
                 dragField_ == kDragPan)
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

        // Load WAV(s) — multi-select via zenity, fallback to DPF single-file browser
        if (loadBtn_.contains(px, py)) {
            if (!filePickerDone_.load(std::memory_order_acquire)) return true;
            filePickerDone_.store(false, std::memory_order_release);
            if (filePickerThread_.joinable()) filePickerThread_.join();
            filePickerIsImport_ = false;
            filePickerThread_ = std::thread([this]() {
                std::vector<std::string> paths;
                bool needFallback = false;

#ifdef __linux__
                // zenity is the standard GTK multi-file picker on Linux desktops.
                // Check availability first so we can fall back immediately if absent.
                FILE* check = ::popen("which zenity >/dev/null 2>&1", "r");
                if (check) {
                    needFallback = (::pclose(check) != 0);
                } else {
                    needFallback = true;
                }

                if (!needFallback) {
                    FILE* pipe = ::popen(
                        "zenity --file-selection --multiple"
                        " --title='Load WAV Files'"
                        " --file-filter='WAV files | *.wav *.WAV'"
                        " 2>/dev/null", "r");
                    if (pipe) {
                        char buf[4096];
                        std::string all;
                        while (std::fgets(buf, sizeof(buf), pipe))
                            all += buf;
                        ::pclose(pipe);
                        // zenity separates multiple paths with '|'; strip trailing newline
                        while (!all.empty() && (all.back() == '\n' || all.back() == '\r'))
                            all.pop_back();
                        std::size_t pos = 0;
                        while (pos <= all.size()) {
                            const std::size_t sep = all.find('|', pos);
                            const std::string p = all.substr(pos, sep == std::string::npos
                                                                  ? std::string::npos : sep - pos);
                            if (!p.empty()) paths.push_back(p);
                            if (sep == std::string::npos) break;
                            pos = sep + 1;
                        }
                    }
                }
#else
                needFallback = true;  // zenity is Linux-only; use DPF browser on macOS/Windows
#endif

                {
                    std::lock_guard<std::mutex> lk(filePickerMtx_);
                    filePickerPaths_    = std::move(paths);
                    filePickerFallback_ = needFallback;
                }
                filePickerDone_.store(true, std::memory_order_release);
            });
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

        // Save Patch — open dialog with pre-filled timestamped path
        if (savePatchBtn_.contains(px, py)) {
            std::time_t t = std::time(nullptr);
            char tbuf[32];
            struct tm* tm_info = std::localtime(&t);
            std::strftime(tbuf, sizeof(tbuf), "patch_%Y%m%d_%H%M%S.ttl", tm_info);
            dialogText_ = dataDir_ + "/" + tbuf;
            dialogMode_ = kDialogSavePatch;
            getWindow().focus();
            repaint();
            return true;
        }

        // Settings
        if (settingsBtn_.contains(px, py)) {
            dialogText_ = dataDir_;
            dialogMode_ = kDialogSettings;
            getWindow().focus();
            repaint();
            return true;
        }

        // Load Patch — open file browser starting at dataDir_
        if (loadPatchBtn_.contains(px, py)) {
            FileBrowserOptions opts;
            opts.startDir = dataDir_.empty() ? nullptr : dataDir_.c_str();
            opts.title    = "Load Patch";
            fileBrowserKey_ = kStateKeyPatchLoad;
            openFileBrowser(opts);
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

        // Import wavetable: pick WAV(s), convert first frame of each, append as zones
        if (importBtn_.contains(px, py)) {
            if (!filePickerDone_.load(std::memory_order_acquire)) return true;
            filePickerDone_.store(false, std::memory_order_release);
            if (filePickerThread_.joinable()) filePickerThread_.join();
            filePickerIsImport_ = true;
            filePickerThread_ = std::thread([this]() {
                std::vector<std::string> paths;
                bool needFallback = false;
#ifdef __linux__
                FILE* check = ::popen("which zenity >/dev/null 2>&1", "r");
                if (check) {
                    needFallback = (::pclose(check) != 0);
                } else {
                    needFallback = true;
                }
                if (!needFallback) {
                    FILE* pipe = ::popen(
                        "zenity --file-selection --multiple"
                        " --title='Import Wavetable(s)'"
                        " --file-filter='WAV files | *.wav *.WAV'"
                        " 2>/dev/null", "r");
                    if (pipe) {
                        char buf[4096];
                        std::string all;
                        while (std::fgets(buf, sizeof(buf), pipe))
                            all += buf;
                        ::pclose(pipe);
                        while (!all.empty() && (all.back() == '\n' || all.back() == '\r'))
                            all.pop_back();
                        std::size_t pos = 0;
                        while (pos <= all.size()) {
                            const std::size_t sep = all.find('|', pos);
                            const std::string p = all.substr(pos, sep == std::string::npos
                                                                  ? std::string::npos : sep - pos);
                            if (!p.empty()) paths.push_back(p);
                            if (sep == std::string::npos) break;
                            pos = sep + 1;
                        }
                    }
                }
#else
                needFallback = true;
#endif
                {
                    std::lock_guard<std::mutex> lk(filePickerMtx_);
                    filePickerPaths_    = std::move(paths);
                    filePickerFallback_ = needFallback;
                }
                filePickerDone_.store(true, std::memory_order_release);
            });
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
            auto flashBtn = [&](int id) {
                flashedBtn_ = id;
                flashedBtnAt_ = std::chrono::steady_clock::now();
                repaint();
            };
            if (waveNormBtn_.contains(px, py)) {
                std::snprintf(buf, sizeof(buf), "%d", si);
                setState(kStateKeyZoneNormalize, buf);
                flashBtn(kFlashNorm);
                return true;
            }
            if (waveTrimBtn_.contains(px, py)) {
                std::snprintf(buf, sizeof(buf), "%d|-60", si);
                setState(kStateKeyZoneTrim, buf);
                flashBtn(kFlashTrim);
                return true;
            }
            if (waveFadeBtn_.contains(px, py)) {
                std::snprintf(buf, sizeof(buf), "%d|10|10", si);
                setState(kStateKeyZoneFade, buf);
                flashBtn(kFlashFade);
                return true;
            }
            if (waveRevBtn_.contains(px, py)) {
                std::snprintf(buf, sizeof(buf), "%d", si);
                setState(kStateKeyZoneReverse, buf);
                flashBtn(kFlashRev);
                return true;
            }
            if (waveRandBtn_.contains(px, py)) {
                if (zones_.size() < 2) { showStatus("Need at least 2 zones to shuffle"); return true; }
                setState(kStateKeyZoneShuffle, "1");
                flashBtn(kFlashRand);
                return true;
            }
            if (waveSliceBtn_.contains(px, py)) {
                std::snprintf(buf, sizeof(buf), "%d|%d|-1", si, sliceCount_);
                setState(kStateKeyZoneSlice, buf);
                return true;
            }
            if (waveSliceDecBtn_.contains(px, py)) {
                if (sliceCount_ > 0) sliceCount_--;
                repaint(); return true;
            }
            if (waveSliceIncBtn_.contains(px, py)) {
                if (sliceCount_ < 64) sliceCount_++;
                repaint(); return true;
            }

            // Drum: delegate full acoustic drum mapping to DSP (has audio data).
            // Filename-only analysis in the UI gives 0 matches for slice files.
            // Snapshot zones so Cancel can revert; flag triggers dialog on kParamZonesVersion bump.
            if (drumBtn_.contains(px, py)) {
                if (zones_.empty()) { showStatus("No zones to assign"); return true; }
                preDrumMapZones_  = zones_;
                pendingDrumDialog_ = true;
                setState(kStateKeyMapDrum, "1");
                showStatus("Analyzing drums\xe2\x80\xa6");
                return true;
            }

            // Spread: distribute zones equally over a 4-octave range (C2=36 … B5=83)
            if (spreadBtn_.contains(px, py)) {
                const int n = static_cast<int>(zones_.size());
                if (n > 0) {
                    constexpr int kLow = 36, kRange = 48;  // C2 – B5
                    for (int zi = 0; zi < n; ++zi) {
                        const int lo   = kLow + zi       * kRange / n;
                        const int hi   = kLow + (zi + 1) * kRange / n - 1;
                        const int root = (lo + hi) / 2;
                        zones_[static_cast<std::size_t>(zi)].rootNote  = root;
                        zones_[static_cast<std::size_t>(zi)].rangeLow  = lo;
                        zones_[static_cast<std::size_t>(zi)].rangeHigh = hi;
                        pushZoneUpdate(static_cast<std::size_t>(zi));
                    }
                }
                repaint();
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

            // ADSR/filter/pan drag slider start
            auto startDspDrag = [&](Rect& r, DragField field, float startVal) -> bool {
                if (r.contains(px, py)) {
                    dragField_      = field;
                    dragZoneIdx_    = si;
                    dragStartY_     = py;
                    dragStartX_     = px;
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
            if (startDspDrag(panSlider_,          kDragPan,          dze.pan))           return true;
        }

        // Zone row interactions
        const float W     = static_cast<float>(getWidth());
        const float H     = static_cast<float>(getHeight());
        const float listY = kHeaderH + kPad * 2.0f;
        const float listH = H - listY - kFooterH - kPad * 1.5f - kWaveH;
        const float rowsY = listY + 23.0f;

        // Delete-all button
        if (deleteAllBtn_.contains(px, py) && !zones_.empty()) {
            // Remove zones from last to first to keep indices valid
            for (int zi = static_cast<int>(zones_.size()) - 1; zi >= 0; --zi) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", zi);
                setState(kStateKeyZoneRemove, buf);
            }
            zones_.clear();
            selectedZone_   = -1;
            previewZoneIdx_ = -1;
            peaks_.clear();
            peakZoneIdx_    = -1;
            showStatus("All zones removed");
            repaint();
            return true;
        }

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
                if (previewZoneIdx_ == static_cast<int>(i))
                    previewZoneIdx_ = -1;
                else if (previewZoneIdx_ > static_cast<int>(i))
                    --previewZoneIdx_;
                repaint();
                return true;
            }

            // Play button — toggle preview
            const Rect playR { W - kPad - 140.0f, ry + 3.0f, 26.0f, 20.0f };
            if (playR.contains(px, py)) {
                selectedZone_ = static_cast<int>(i);
                if (peakZoneIdx_ != selectedZone_)
                    loadPeaks(selectedZone_);
                char buf[16];
                if (previewZoneIdx_ == static_cast<int>(i)) {
                    // Stop playback
                    previewZoneIdx_ = -1;
                    std::snprintf(buf, sizeof(buf), "-1");
                } else {
                    // Start playback
                    previewZoneIdx_ = static_cast<int>(i);
                    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(i));
                }
                setState(kStateKeyZonePreview, buf);
                repaint();
                return true;
            }

            // Mute toggle
            const Rect muteR { W - kPad - 60.0f, ry + 3.0f, 36.0f, 20.0f };
            if (muteR.contains(px, py)) {
                selectedZone_ = static_cast<int>(i);
                if (peakZoneIdx_ != selectedZone_)
                    loadPeaks(selectedZone_);
                zones_[i].muted = !zones_[i].muted;
                pushZoneDspUpdate(i);
                repaint();
                return true;
            }

            // Loop toggle
            const Rect loopR { W - kPad - 110.0f, ry + 3.0f, 46.0f, 20.0f };
            if (loopR.contains(px, py)) {
                selectedZone_ = static_cast<int>(i);
                if (peakZoneIdx_ != selectedZone_)
                    loadPeaks(selectedZone_);
                zones_[i].loopEnabled = !zones_[i].loopEnabled;
                // When enabling loop with uninitialised endpoints, set loopEnd
                // to the full sample length so both handles are immediately draggable.
                if (zones_[i].loopEnabled && zones_[i].loopEnd == 0 && zones_[i].totalFrames > 0)
                    zones_[i].loopEnd = zones_[i].totalFrames - 1;
                pushZoneUpdate(i);
                repaint();
                return true;
            }

            // Reorder grab handle (left of Root column — the '#' number area)
            const Rect grabR { kPad, ry, kColRoot - 4.0f, kRowH };
            if (grabR.contains(px, py)) {
                dragField_        = kDragZoneReorder;
                reorderFromIdx_   = static_cast<int>(i);
                reorderTargetIdx_ = static_cast<int>(i);
                dragStartY_       = py;
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

            // Octave shift — drag start (up = increase, down = decrease; 8px/octave)
            const Rect octaveR { kPad + kColOctave, ry, 46.0f, kRowH };
            if (octaveR.contains(px, py)) {
                dragField_     = kDragOctave;
                dragZoneIdx_   = static_cast<int>(i);
                dragStartY_    = py;
                dragStartNote_ = zones_[i].octaveShift;
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
                selectedZone_ = static_cast<int>(i);
                // Reload peaks if not already loaded for this zone (retry on prior failure too)
                if (peakZoneIdx_ != selectedZone_)
                    loadPeaks(selectedZone_);
                repaint();
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float py = static_cast<float>(ev.pos.getY());
        const float px = static_cast<float>(ev.pos.getX());

        if (dragField_ == kDragZoneReorder) {
            // Compute insertion target from cursor position in the zone list
            const float H2    = static_cast<float>(getHeight());
            const float listY = kHeaderH + kPad * 2.0f;
            const float listH = H2 - listY - kFooterH - kPad * 1.5f - kWaveH;
            const float rowsY = listY + 23.0f;
            const int n       = static_cast<int>(zones_.size());
            // Find which gap the cursor is closest to (0 = above row 0, n = below last row)
            int target = n;
            for (int i = static_cast<int>(zoneScrollOffset_); i < n; ++i) {
                const float ry = rowsY + static_cast<float>(i - zoneScrollOffset_) * kRowH;
                if (ry > listY + listH) break;
                if (py < ry + kRowH * 0.5f) { target = i; break; }
            }
            if (reorderTargetIdx_ != target) {
                reorderTargetIdx_ = target;
                repaint();
            }
            return true;
        }

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
             dragField_ == kDragFilterCutoff || dragField_ == kDragFilterQ ||
             dragField_ == kDragPan)
            && dragZoneIdx_ >= 0 && dragZoneIdx_ < static_cast<int>(zones_.size()))
        {
            ZoneEntry& dz = zones_[static_cast<std::size_t>(dragZoneIdx_)];
            const float dy = dragStartY_ - py;  // upward = increase
            if (dragField_ == kDragAttack) {
                dz.attackMs = std::clamp(dragStartFloat_ + dy * 6.0f, 0.0f, 5000.0f);
            } else if (dragField_ == kDragDecay) {
                dz.decayMs = std::clamp(dragStartFloat_ + dy * 6.0f, 0.0f, 5000.0f);
            } else if (dragField_ == kDragSustain) {
                dz.sustainLevel = std::clamp(dragStartFloat_ + dy / 33.0f, 0.0f, 1.0f);
            } else if (dragField_ == kDragRelease) {
                dz.releaseMs = std::clamp(dragStartFloat_ + dy * 6.0f, 0.0f, 10000.0f);
            } else if (dragField_ == kDragPan) {
                dz.pan = std::clamp(dragStartFloat_ + dy / 33.0f, -1.0f, 1.0f);
            } else if (dragField_ == kDragFilterCutoff) {
                const float factor = std::pow(2.0f, dy / 20.0f);
                dz.filterCutoffHz = std::clamp(dragStartFloat_ * factor, 20.0f, 20000.0f);
            } else if (dragField_ == kDragFilterQ) {
                dz.filterQ = std::clamp(dragStartFloat_ + dy / 33.0f, 0.1f, 20.0f);
            }
            pushZoneDspUpdate(static_cast<std::size_t>(dragZoneIdx_));
            repaint();
            return true;
        }

        if (dragField_ == kDragOctave && dragZoneIdx_ >= 0
            && dragZoneIdx_ < static_cast<int>(zones_.size()))
        {
            const int delta  = static_cast<int>((dragStartY_ - py) / 8.0f);
            ZoneEntry& z     = zones_[static_cast<std::size_t>(dragZoneIdx_)];
            z.octaveShift    = std::clamp(dragStartNote_ + delta, -6, 6);
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
        if (dialogMode_ == kDialogDrumReport) {
            constexpr float kRptRowH  = 22.0f;
            constexpr float kRptBodyH = 280.0f;
            const int visible   = static_cast<int>(kRptBodyH / kRptRowH);
            const int maxScroll = std::max(0, static_cast<int>(drumReportAssignments_.size()) - visible);
            drumReportScrollOffset_ = std::clamp(
                drumReportScrollOffset_ - static_cast<int>(ev.delta.getY()), 0, maxScroll);
            repaint();
            return true;
        }

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

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (dialogMode_ == kDialogNone) return false;
        if (!ev.press) return true; // swallow key-release too while dialog is open

        const uint32_t k = ev.key;
        if (k == 27) { // Escape
            dialogMode_ = kDialogNone;
            repaint();
            return true;
        }
        if (k == 13 || k == '\n') { // Enter
            confirmDialog();
            return true;
        }
        if (k == 8 && !dialogText_.empty()) { // Backspace
            dialogText_.pop_back();
            repaint();
            return true;
        }
        if (ev.key >= 0x20 && ev.key < 0x7f) {
            dialogText_ += static_cast<char>(ev.key);
            repaint();
            return true;
        }
        return true; // swallow all other keys while dialog is open
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename && filename[0] != '\0' && !fileBrowserKey_.empty())
            setState(fileBrowserKey_.c_str(), filename);
        fileBrowserKey_.clear();
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
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%d|%d|%d|%d|%d|%u|%u|%d",
                      static_cast<int>(idx),
                      z.rootNote, z.rangeLow, z.rangeHigh,
                      z.loopEnabled ? 1 : 0,
                      z.loopStart, z.loopEnd,
                      z.octaveShift);
        setState(kStateKeyZoneUpdate, buf);
    }

    void pushZoneDspUpdate(std::size_t idx)
    {
        if (idx >= zones_.size()) return;
        const ZoneEntry& z = zones_[idx];
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%d|%.4f|%.4f|%.4f|%.4f|%d|%d|%.2f|%.4f|%.4f|%d",
                      static_cast<int>(idx),
                      z.attackMs, z.decayMs, z.sustainLevel, z.releaseMs,
                      z.filterEnabled ? 1 : 0, z.filterType,
                      z.filterCutoffHz, z.filterQ, z.pan,
                      z.muted ? 1 : 0);
        setState(kStateKeyZoneDsp, buf);
    }

    // Queue a waveform peak load. Non-blocking: the actual I/O runs on a
    // background thread and uiIdle applies the result when ready.
    void loadPeaks(int idx)
    {
        if (idx == peakZoneIdx_ && !peakLoadFailed_) return;
        peaks_.clear();
        peakZoneIdx_    = -1;
        peakLoadFailed_ = false;
        peakPendingIdx_ = idx;
    }

    void launchPeakThread(int idx, const std::string& path)
    {
        peakThreadDone_.store(false, std::memory_order_release);
        peakThread_ = std::thread([this, idx, path]() {
            PeakResult r;
            r.zoneIdx = idx;
            auto loaded = downspout::campione::loadWavZone(path);
            if (!loaded.error.empty() || loaded.zone.data.empty()) {
                r.failed = true;
            } else {
                const auto& data  = loaded.zone.data;
                const int ch      = loaded.zone.channelCount;
                const int totalF  = static_cast<int>(data.size()) / std::max(1, ch);
                r.totalFrames     = static_cast<uint32_t>(totalF);
                constexpr int kPeakBins = 400;
                r.peaks.resize(kPeakBins * 2, 0.0f);
                for (int bin = 0; bin < kPeakBins; ++bin) {
                    const int f0 = static_cast<int>(static_cast<int64_t>(bin)     * totalF / kPeakBins);
                    const int f1 = static_cast<int>(static_cast<int64_t>(bin + 1) * totalF / kPeakBins);
                    float mn = 0.0f, mx = 0.0f;
                    for (int f = f0; f < f1; ++f) {
                        float s = 0.0f;
                        for (int c = 0; c < ch; ++c)
                            s += data[static_cast<std::size_t>(f * ch + c)];
                        s /= static_cast<float>(std::max(1, ch));
                        mn = std::min(mn, s); mx = std::max(mx, s);
                    }
                    r.peaks[bin * 2 + 0] = mn;
                    r.peaks[bin * 2 + 1] = mx;
                }
            }
            { std::lock_guard<std::mutex> lk(peakResultMtx_); peakThreadResult_ = std::move(r); }
            peakThreadDone_.store(true, std::memory_order_release);
        });
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
        text(kPad + kColNum,    hy, "#",     nullptr);
        text(kPad + kColRoot,   hy, "Root",  nullptr);
        text(kPad + kColOctave, hy, "Oct",   nullptr);
        text(kPad + kColRange,  hy, "Range", nullptr);
        text(W - kPad - 150.0f, hy, "Loop", nullptr);
        text(W - kPad - 68.0f,  hy, "Mute", nullptr);
        text(kPad + kColFile,   hy, "File",  nullptr);

        // Delete-all button (header row, same column as per-row remove button)
        deleteAllBtn_ = { W - kPad - 22.0f, listY + 4.0f, 18.0f, 16.0f };
        beginPath();
        fillColor(120, 36, 36, 255);
        roundedRect(deleteAllBtn_.x, deleteAllBtn_.y, deleteAllBtn_.w, deleteAllBtn_.h, 4.0f);
        fill();
        closePath();
        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(220, 140, 140, 255);
        text(deleteAllBtn_.x + deleteAllBtn_.w * 0.5f,
             deleteAllBtn_.y + deleteAllBtn_.h * 0.5f, "All", nullptr);

        beginPath();
        fillColor(50, 62, 72, 255);
        rect(kPad + 1.0f, listY + 22.0f, W - kPad * 2.0f - 2.0f, 1.0f);
        fill();
        closePath();

        const float rowsY = listY + 23.0f;
        for (std::size_t i = static_cast<std::size_t>(zoneScrollOffset_); i < zones_.size(); ++i) {
            const float ry  = rowsY + static_cast<float>(i - static_cast<std::size_t>(zoneScrollOffset_)) * kRowH;
            if (ry + kRowH > listY + listH) break;

            const ZoneEntry& z           = zones_[i];
            const bool even              = (i % 2) == 0;
            const bool isDragged         = dragZoneIdx_ == static_cast<int>(i) && dragField_ != kDragNone;
            const bool isReorderSource   = dragField_ == kDragZoneReorder && reorderFromIdx_ == static_cast<int>(i);
            const bool isSelected        = selectedZone_ == static_cast<int>(i);

            beginPath();
            if (isReorderSource)
                fillColor(40, 56, 72, 180);  // dim while being dragged
            else if (isSelected)
                fillColor(28, 42, 60, 255);
            else
                fillColor(even ? 22 : 18, even ? 31 : 26,
                          isDragged ? 50 : (even ? 41 : 34), 255);
            rect(kPad + 1.0f, ry, W - kPad * 2.0f - 2.0f, kRowH - 1.0f);
            fill();
            closePath();

            // Drop indicator line above this row when reorderTargetIdx_ == i
            if (dragField_ == kDragZoneReorder && reorderTargetIdx_ == static_cast<int>(i)) {
                beginPath();
                fillColor(82, 162, 240, 220);
                rect(kPad + 1.0f, ry - 2.0f, W - kPad * 2.0f - 2.0f, 3.0f);
                fill();
                closePath();
            }

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

            // Octave shift
            if (isDragged && dragField_ == kDragOctave)
                fillColor(240, 205, 170, 255);
            else
                fillColor(z.octaveShift != 0 ? 180 : 120, 180, z.octaveShift != 0 ? 100 : 170, 255);
            std::snprintf(buf, sizeof(buf), "%+d", z.octaveShift);
            text(kPad + kColOctave, mid, buf, nullptr);
            fillColor(200, 205, 215, 255);

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

            // Play button (one-shot preview)
            const bool isPlaying = (previewZoneIdx_ == static_cast<int>(i));
            const Rect playR { W - kPad - 140.0f, ry + 3.0f, 26.0f, 20.0f };
            beginPath();
            fillColor(isPlaying ? 32 : 22, isPlaying ? 100 : 60, isPlaying ? 32 : 22, 255);
            roundedRect(playR.x, playR.y, playR.w, playR.h, 5.0f);
            fill();
            closePath();
            beginPath();
            strokeColor(isPlaying ? 80 : 60, isPlaying ? 220 : 140, isPlaying ? 80 : 60, 220);
            strokeWidth(1.0f);
            roundedRect(playR.x, playR.y, playR.w, playR.h, 5.0f);
            stroke();
            closePath();
            fontSize(11.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(isPlaying ? 80 : 60, isPlaying ? 230 : 160, isPlaying ? 80 : 60, 255);
            text(playR.x + playR.w * 0.5f, playR.y + playR.h * 0.5f,
                 "\xe2\x96\xb6", nullptr);

            // Loop button
            const Rect loopR { W - kPad - 110.0f, ry + 3.0f, 46.0f, 20.0f };
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

            // Mute button
            const Rect muteR { W - kPad - 60.0f, ry + 3.0f, 36.0f, 20.0f };
            beginPath();
            fillColor(z.muted ? 100 : 36, z.muted ? 30 : 36, z.muted ? 30 : 36, 255);
            roundedRect(muteR.x, muteR.y, muteR.w, muteR.h, 5.0f);
            fill();
            closePath();
            beginPath();
            strokeColor(z.muted ? 220 : 82, z.muted ? 80 : 112, z.muted ? 80 : 112,
                        z.muted ? 220 : 110);
            strokeWidth(1.0f);
            roundedRect(muteR.x, muteR.y, muteR.w, muteR.h, 5.0f);
            stroke();
            closePath();
            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(z.muted ? 240 : 178, z.muted ? 100 : 120, z.muted ? 100 : 120, 255);
            text(muteR.x + muteR.w * 0.5f, muteR.y + muteR.h * 0.5f, "MUTE", nullptr);

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

            // Drag grip dots in the '#' column
            if (dragField_ == kDragZoneReorder && reorderFromIdx_ == static_cast<int>(i)) {
                fillColor(82, 162, 240, 200);
            } else {
                fillColor(70, 88, 100, 180);
            }
            for (int dot = 0; dot < 3; ++dot) {
                const float dx = kPad + 4.0f;
                const float dy = ry + 7.0f + static_cast<float>(dot) * 5.0f;
                beginPath();
                circle(dx, dy, 1.5f);
                fill();
                closePath();
                beginPath();
                circle(dx + 5.0f, dy, 1.5f);
                fill();
                closePath();
            }
        }

        // Drop indicator below the last visible row
        if (dragField_ == kDragZoneReorder
            && reorderTargetIdx_ == static_cast<int>(zones_.size()))
        {
            const int lastVisible = std::min(static_cast<int>(zones_.size()),
                                             zoneScrollOffset_ + static_cast<int>(listH / kRowH));
            const float lineY = rowsY + static_cast<float>(lastVisible - zoneScrollOffset_) * kRowH - 2.0f;
            if (lineY < listY + listH) {
                beginPath();
                fillColor(82, 162, 240, 220);
                rect(kPad + 1.0f, lineY, W - kPad * 2.0f - 2.0f, 3.0f);
                fill();
                closePath();
            }
        }

        if (zones_.empty()) {
            fontSize(13.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(80, 95, 108, 255);
            text(W * 0.5f, listY + listH * 0.5f,
                 "No zones loaded \xe2\x80\x94 click Load WAV(s) or Record", nullptr);
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
        } else if (!z.path.empty()) {
            fontSize(11.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(peakLoadFailed_ ? 160 : 72,
                      peakLoadFailed_ ?  80 : 95,
                      peakLoadFailed_ ?  80 : 112, 255);
            text(waveRect_.x + waveRect_.w * 0.5f, waveRect_.y + kWaveDisplayH * 0.5f,
                 peakLoadFailed_ ? "file not found" : "loading\xe2\x80\xa6", nullptr);
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

        // Action bar (Normalize / Trim / Fade / Reverse / Shuffle)
        {
            constexpr float kActionH = 16.0f;
            // Place action bar above the ADSR row, leaving 52px at bottom for ADSR/filter
            const float ay = waveRect_.y + waveRect_.h - 52.0f - kActionH - 2.0f;
            const float btnW = 66.0f;
            const float gap  = 6.0f;
            float bx = waveRect_.x + 6.0f;

            using Clock = std::chrono::steady_clock;
            const auto flashAge = std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - flashedBtnAt_).count();
            const bool flashActive = (flashedBtn_ != kFlashNone && flashAge < 200);

            // drawActionBtn: colored teal style, brightens briefly when flashing
            auto drawActionBtn = [&](Rect& out, const char* label, int btnId) {
                out = { bx, ay, btnW, kActionH };
                const bool lit = flashActive && (flashedBtn_ == btnId);
                beginPath();
                fillColor(lit ? 38 : 18, lit ? 88 : 55, lit ? 80 : 52, 240);
                roundedRect(out.x, out.y, out.w, out.h, 4.0f);
                fill();
                closePath();
                beginPath();
                strokeColor(lit ? 80 : 45, lit ? 190 : 120, lit ? 170 : 110, lit ? 255 : 200);
                strokeWidth(lit ? 1.5f : 1.0f);
                roundedRect(out.x, out.y, out.w, out.h, 4.0f);
                stroke();
                closePath();
                fontSize(9.0f);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fillColor(lit ? 200 : 110, lit ? 255 : 215, lit ? 240 : 195, 255);
                text(out.x + out.w * 0.5f, out.y + out.h * 0.5f, label, nullptr);
                bx += btnW + gap;
            };

            drawActionBtn(waveNormBtn_, "Normalize", kFlashNorm);
            drawActionBtn(waveTrimBtn_, "Trim",      kFlashTrim);
            drawActionBtn(waveFadeBtn_, "Fade",      kFlashFade);
            drawActionBtn(waveRevBtn_,  "Reverse",   kFlashRev);
            drawActionBtn(waveRandBtn_, "Shuffle",   kFlashRand);

            // Slice controls: [−][N][+]  [Slice ▸]
            {
                // Count spin: − box, count display, + box
                const float spinW = 16.0f, countW = 34.0f;
                auto drawSpinBtn = [&](Rect& out, float x, const char* label) {
                    out = { x, ay, spinW, kActionH };
                    beginPath(); fillColor(24, 38, 50, 240);
                    roundedRect(x, ay, spinW, kActionH, 3.0f); fill(); closePath();
                    beginPath(); strokeColor(55, 80, 100, 160); strokeWidth(1.0f);
                    roundedRect(x, ay, spinW, kActionH, 3.0f); stroke(); closePath();
                    fontSize(10.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                    fillColor(160, 185, 205, 255);
                    text(x + spinW * 0.5f, ay + kActionH * 0.5f, label, nullptr);
                };
                drawSpinBtn(waveSliceDecBtn_, bx, "\xe2\x88\x92");  // minus sign
                bx += spinW;
                // Count display
                waveSliceCountRect_ = { bx, ay, countW, kActionH };
                beginPath(); fillColor(20, 32, 44, 240);
                roundedRect(bx, ay, countW, kActionH, 0.0f); fill(); closePath();
                fontSize(9.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fillColor(190, 210, 225, 255);
                char countBuf[12];
                std::snprintf(countBuf, sizeof(countBuf), sliceCount_ == 0 ? "auto" : "%d", sliceCount_);
                text(bx + countW * 0.5f, ay + kActionH * 0.5f, countBuf, nullptr);
                bx += countW;
                drawSpinBtn(waveSliceIncBtn_, bx, "+");
                bx += spinW + gap;
                // Slice trigger button
                drawActionBtn(waveSliceBtn_, "Slice \xe2\x96\xb8");
            }

            // Zone-layout buttons (operate on all zones)
            bx += gap;
            drawActionBtn(drumBtn_,   "Drum");
            drawActionBtn(spreadBtn_, "Spread");
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

            // Helper: draw a rotary knob.
            // t = normalised 0..1. arc() is only called when sweep > 0 to avoid crashes.
            constexpr float kStartAng = 0.75f * 3.14159265f;  // lower-left (~8 o'clock)
            constexpr float kSweep    = 1.5f  * 3.14159265f;  // 270°

            auto drawKnob = [&](Rect& out, float bx, float by, float bw,
                                const char* label, const char* valStr,
                                float t, bool active) {
                out = { bx, by, bw, 48.0f };
                const float r = std::min(bw * 0.40f, 12.0f);
                if (r <= 0.5f) return;
                const float cx = bx + bw * 0.5f;
                const float cy = by + 10.0f + r;
                const float tc = std::clamp(t, 0.0f, 1.0f);
                const float endAng = kStartAng + tc * kSweep;

                // Label
                fontSize(7.5f);
                textAlign(ALIGN_CENTER | ALIGN_TOP);
                fillColor(108, 130, 148, 255);
                text(cx, by + 1.0f, label, nullptr);

                // Knob body
                beginPath();
                circle(cx, cy, r);
                fillColor(active ? 30 : 22, active ? 46 : 34, active ? 62 : 46, 255);
                fill();

                // Track ring (always draw full 270° — sweep is constant, never zero)
                beginPath();
                arc(cx, cy, r - 1.5f, kStartAng, kStartAng + kSweep, CW);
                strokeColor(38, 55, 72, 255);
                strokeWidth(2.5f);
                stroke();

                // Fill arc (only draw when value is meaningfully above min)
                const float fillSweep = tc * kSweep;
                if (fillSweep > 0.01f) {
                    beginPath();
                    arc(cx, cy, r - 1.5f, kStartAng, kStartAng + fillSweep, CW);
                    strokeColor(active ? 80 : 52, active ? 168 : 126, active ? 214 : 172, 255);
                    strokeWidth(2.5f);
                    stroke();
                }

                // Indicator line from centre to rim
                {
                    const float ix = cx + (r - 3.5f) * std::cos(endAng);
                    const float iy = cy + (r - 3.5f) * std::sin(endAng);
                    beginPath();
                    moveTo(cx, cy);
                    lineTo(ix, iy);
                    strokeColor(active ? 235 : 185, active ? 218 : 198, active ? 185 : 215, 255);
                    strokeWidth(1.5f);
                    stroke();
                }

                // Value label below knob
                fontSize(7.5f);
                textAlign(ALIGN_CENTER | ALIGN_TOP);
                fillColor(active ? 235 : 165, active ? 218 : 185, active ? 185 : 205, 255);
                text(cx, cy + r + 2.0f, valStr, nullptr);
            };

            char buf[32];
            const float slotW = (waveRect_.w - 12.0f) / 9.0f;
            float sx = waveRect_.x + 6.0f;

            // Attack
            std::snprintf(buf, sizeof(buf), "%.0fms", dz.attackMs);
            drawKnob(adsrAttackSlider_,  sx, ry + 2.0f, slotW - 2.0f, "ATK", buf,
                     dz.attackMs / 5000.0f,
                     dragField_ == kDragAttack && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Decay
            std::snprintf(buf, sizeof(buf), "%.0fms", dz.decayMs);
            drawKnob(adsrDecaySlider_,   sx, ry + 2.0f, slotW - 2.0f, "DEC", buf,
                     dz.decayMs / 5000.0f,
                     dragField_ == kDragDecay && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Sustain
            std::snprintf(buf, sizeof(buf), "%.2f", dz.sustainLevel);
            drawKnob(adsrSustainSlider_, sx, ry + 2.0f, slotW - 2.0f, "SUS", buf,
                     dz.sustainLevel,
                     dragField_ == kDragSustain && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Release
            std::snprintf(buf, sizeof(buf), "%.0fms", dz.releaseMs);
            drawKnob(adsrReleaseSlider_, sx, ry + 2.0f, slotW - 2.0f, "REL", buf,
                     dz.releaseMs / 10000.0f,
                     dragField_ == kDragRelease && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Pan — centre (pan=0) maps to t=0.5 (12 o'clock)
            if (dz.pan == 0.0f)
                std::snprintf(buf, sizeof(buf), "C");
            else
                std::snprintf(buf, sizeof(buf), dz.pan < 0.0f ? "L%.0f" : "R%.0f",
                              std::abs(dz.pan) * 100.0f);
            drawKnob(panSlider_, sx, ry + 2.0f, slotW - 2.0f, "PAN", buf,
                     (dz.pan + 1.0f) * 0.5f,
                     dragField_ == kDragPan && dragZoneIdx_ == selectedZone_);
            sx += slotW;

            // Filter enable toggle (kept as button)
            filterEnableBtn_ = { sx + 1.0f, ry + 3.0f, slotW - 4.0f, 44.0f };
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

            // Filter type toggle (kept as button)
            static const char* kFltNames[] = {"LP","BP","HP","NT"};
            filterTypeBtn_ = { sx + 1.0f, ry + 3.0f, slotW - 4.0f, 44.0f };
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

            // Cutoff — logarithmic mapping 20..20000 Hz
            {
                const float logT = std::log(std::max(dz.filterCutoffHz, 20.0f) / 20.0f)
                                   / std::log(1000.0f);
                std::snprintf(buf, sizeof(buf), "%.0fHz", dz.filterCutoffHz);
                drawKnob(filterCutoffSlider_, sx, ry + 2.0f, slotW - 2.0f, "CUTOFF", buf,
                         logT,
                         dragField_ == kDragFilterCutoff && dragZoneIdx_ == selectedZone_);
            }
            sx += slotW;

            // Q — linear 0.1..20
            std::snprintf(buf, sizeof(buf), "%.2f", dz.filterQ);
            drawKnob(filterQSlider_, sx, ry + 2.0f, slotW - 2.0f, "Q", buf,
                     (dz.filterQ - 0.1f) / 19.9f,
                     dragField_ == kDragFilterQ && dragZoneIdx_ == selectedZone_);
            (void)sx;
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

        // Footer buttons — evenly packed from left; MCP anchored to right
        //   [Load WAV][Record][Save Patch][Load Patch][Settings] ... [MCP]
        constexpr float kBtnW = 90.0f, kBtnH = 28.0f, kBtnGap = 8.0f;
        float bx = kPad;

        loadBtn_ = { bx, fy + 16.0f, kBtnW, kBtnH };
        if (!filePickerDone_.load(std::memory_order_relaxed) && !filePickerIsImport_)
            drawButton(loadBtn_, "Loading…", 110, 80, 20);
        else
            drawButton(loadBtn_, "Load WAV(s)", 51, 64, 74);
        bx += kBtnW + kBtnGap;

        recBtn_ = { bx, fy + 16.0f, kBtnW, kBtnH };
        if (recording_) {
            drawButton(recBtn_, "\xe2\x97\x8f  Stop", 140, 40, 40);
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - recordStart_).count();
            char rbuf[16];
            std::snprintf(rbuf, sizeof(rbuf), "%lld:%02lld",
                          static_cast<long long>(elapsed / 60),
                          static_cast<long long>(elapsed % 60));
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(220, 100, 100, 255);
            text(bx + kBtnW + 4.0f, fy + 30.0f, rbuf, nullptr);
        } else {
            drawButton(recBtn_, "\xe2\x97\x8f  Record", 80, 36, 36);
        }
        bx += kBtnW + kBtnGap;

        importBtn_ = { bx, fy + 16.0f, kBtnW, kBtnH };
        drawButton(importBtn_, "Import WT", 46, 68, 58);
        bx += kBtnW + kBtnGap;

        savePatchBtn_ = { bx, fy + 16.0f, kBtnW, kBtnH };
        drawButton(savePatchBtn_, "Save Patch", 36, 64, 80);
        bx += kBtnW + kBtnGap;

        loadPatchBtn_ = { bx, fy + 16.0f, kBtnW, kBtnH };
        drawButton(loadPatchBtn_, "Load Patch", 36, 64, 80);
        bx += kBtnW + kBtnGap;

        settingsBtn_ = { bx, fy + 16.0f, 76.0f, kBtnH };
        drawButton(settingsBtn_, "Settings", 46, 54, 62);

        // Status flash (between Settings and MCP)
        if (!statusMsg_.empty()) {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - statusMsgAt_).count();
            if (age < 2500) {
                const uint8_t alpha = static_cast<uint8_t>(255 - age * 255 / 2500);
                const bool isError = statusMsg_.rfind("Error", 0) == 0;
                fontSize(11.0f);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                if (isError)
                    fillColor(220, 80, 80, alpha);
                else
                    fillColor(80, 200, 120, alpha);
                text(bx + 84.0f, fy + 30.0f, statusMsg_.c_str(), nullptr);
            } else {
                statusMsg_.clear();
                patchSaved_ = false;
            }
        }

        // MCP toggle — right-anchored
        mcpBtn_ = { W - kPad - 72.0f, fy + 16.0f, 72.0f, kBtnH };
        const bool mcpOn = values_[kParamMcpEnabled] >= 0.5f;
        drawButton(mcpBtn_, mcpOn ? "MCP ON" : "MCP OFF",
                   mcpOn ? 30 : 51, mcpOn ? 100 : 64, mcpOn ? 30 : 74);
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

    // ── Context menu ─────────────────────────────────────────────────────────

    struct ContextItem { const char* label; bool separator; };

    // Returns bounds (mx, my, totalH) after position clamping.
    void contextMenuGeometry(float W, float H,
                             float& outMx, float& outMy, float& outTotalH,
                             const ContextItem* items, int count) const
    {
        constexpr float kItemH = 22.0f, kSepH = 8.0f, kMenuW = 130.0f;
        float totalH = 8.0f;
        for (int i = 0; i < count; ++i)
            totalH += items[i].separator ? kSepH : kItemH;
        float mx = contextMenuX_;
        float my = contextMenuY_;
        if (mx + kMenuW > W - kPad) mx = W - kPad - kMenuW;
        if (my + totalH > H - kPad) my = H - kPad - totalH;
        outMx = mx; outMy = my; outTotalH = totalH;
    }

    void drawContextMenu(float W, float H)
    {
        constexpr float kItemH = 22.0f, kSepH = 8.0f, kMenuW = 130.0f, kPadX = 10.0f;
        static const ContextItem items[] = {
            { "Load WAV",   false },
            { "Import WT",  false },
            { nullptr,      true  },
            { "Record",     false },
            { nullptr,      true  },
            { "Save Patch", false },
            { "Load Patch", false },
            { nullptr,      true  },
            { "Clear All",  false },
            { "Settings",   false },
            { "MCP Toggle", false },
        };
        constexpr int n = static_cast<int>(sizeof(items) / sizeof(items[0]));

        float mx, my, totalH;
        contextMenuGeometry(W, H, mx, my, totalH, items, n);

        // Shadow
        beginPath();
        fillColor(0, 0, 0, 80);
        roundedRect(mx + 3.0f, my + 3.0f, kMenuW, totalH, 6.0f);
        fill();
        closePath();
        // Background
        beginPath();
        fillColor(32, 42, 52, 248);
        roundedRect(mx, my, kMenuW, totalH, 6.0f);
        fill();
        closePath();
        beginPath();
        strokeColor(80, 100, 120, 200);
        strokeWidth(1.0f);
        roundedRect(mx, my, kMenuW, totalH, 6.0f);
        stroke();
        closePath();

        float iy = my + 4.0f;
        for (int i = 0; i < n; ++i) {
            if (items[i].separator) {
                beginPath();
                fillColor(55, 70, 82, 200);
                rect(mx + 6.0f, iy + kSepH * 0.5f - 0.5f, kMenuW - 12.0f, 1.0f);
                fill();
                closePath();
                iy += kSepH;
            } else {
                fontSize(11.5f);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fillColor(210, 225, 238, 255);
                text(mx + kPadX, iy + kItemH * 0.5f, items[i].label, nullptr);
                iy += kItemH;
            }
        }
    }

    void dispatchContextMenuClick(float px, float py)
    {
        constexpr float kItemH = 22.0f, kSepH = 8.0f, kMenuW = 130.0f;
        static const ContextItem items[] = {
            { "Load WAV",   false },
            { "Import WT",  false },
            { nullptr,      true  },
            { "Record",     false },
            { nullptr,      true  },
            { "Save Patch", false },
            { "Load Patch", false },
            { nullptr,      true  },
            { "Clear All",  false },
            { "Settings",   false },
            { "MCP Toggle", false },
        };
        constexpr int n = static_cast<int>(sizeof(items) / sizeof(items[0]));

        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());
        float mx, my, totalH;
        contextMenuGeometry(W, H, mx, my, totalH, items, n);

        // Click outside → dismiss
        if (px < mx || px > mx + kMenuW || py < my || py > my + totalH) {
            dialogMode_ = kDialogNone;
            repaint();
            return;
        }

        // Find which item was clicked
        float iy = my + 4.0f;
        for (int i = 0; i < n; ++i) {
            if (items[i].separator) {
                iy += kSepH;
            } else {
                if (py >= iy && py < iy + kItemH) {
                    dialogMode_ = kDialogNone;
                    repaint();
                    executeContextItem(i);
                    return;
                }
                iy += kItemH;
            }
        }
        dialogMode_ = kDialogNone;
        repaint();
    }

    void executeContextItem(int idx)
    {
        switch (idx) {
        case 0: // Load WAV
            if (filePickerDone_.load(std::memory_order_acquire)) {
                filePickerDone_.store(false, std::memory_order_release);
                if (filePickerThread_.joinable()) filePickerThread_.join();
                filePickerIsImport_ = false;
                triggerZenityOrFallback(kStateKeyZoneLoad);
            }
            break;
        case 1: // Import WT
            if (filePickerDone_.load(std::memory_order_acquire)) {
                filePickerDone_.store(false, std::memory_order_release);
                if (filePickerThread_.joinable()) filePickerThread_.join();
                filePickerIsImport_ = true;
                triggerZenityOrFallback(kStateKeyWavetableImport);
            }
            break;
        case 2: // separator — never reached
            break;
        case 3: // Record
            recording_ = !recording_;
            if (recording_) recordStart_ = std::chrono::steady_clock::now();
            editParameter(kParamRecording, true);
            setParameterValue(kParamRecording, recording_ ? 1.0f : 0.0f);
            editParameter(kParamRecording, false);
            repaint();
            break;
        case 4: // separator
            break;
        case 5: { // Save Patch
            std::time_t t = std::time(nullptr);
            char tbuf[32];
            std::strftime(tbuf, sizeof(tbuf), "patch_%Y%m%d_%H%M%S.ttl", std::localtime(&t));
            dialogText_ = dataDir_ + "/" + tbuf;
            dialogMode_ = kDialogSavePatch;
            getWindow().focus();
            repaint();
            break;
        }
        case 6: { // Load Patch
            FileBrowserOptions opts;
            opts.startDir = dataDir_.empty() ? nullptr : dataDir_.c_str();
            opts.title    = "Load Patch";
            fileBrowserKey_ = kStateKeyPatchLoad;
            openFileBrowser(opts);
            break;
        }
        case 7: // separator — never reached
            break;
        case 8: // Clear All
            setState(kStateKeyZoneClear, "1");
            zones_.clear();
            selectedZone_ = -1;
            repaint();
            break;
        case 9: // Settings
            dialogText_ = dataDir_;
            dialogMode_ = kDialogSettings;
            getWindow().focus();
            repaint();
            break;
        case 10: { // MCP Toggle
            const float newVal = values_[kParamMcpEnabled] >= 0.5f ? 0.0f : 1.0f;
            values_[kParamMcpEnabled] = newVal;
            editParameter(kParamMcpEnabled, true);
            setParameterValue(kParamMcpEnabled, newVal);
            editParameter(kParamMcpEnabled, false);
            repaint();
            break;
        }
        default: break;
        }
    }

    // Shared zenity/fallback file picker launcher used by buttons and context menu.
    void triggerZenityOrFallback(const char* stateKey)
    {
        const bool isImport = (stateKey == kStateKeyWavetableImport);
        filePickerThread_ = std::thread([this, isImport, stateKeyStr = std::string(stateKey)]() {
            std::vector<std::string> paths;
            bool needFallback = false;
#ifdef __linux__
            FILE* check = ::popen("which zenity >/dev/null 2>&1", "r");
            if (check) {
                needFallback = (::pclose(check) != 0);
            } else {
                needFallback = true;
            }
            if (!needFallback) {
                FILE* pipe = ::popen("zenity --file-selection --multiple --file-filter='WAV files (*.wav)|*.wav' --separator='\\n' 2>/dev/null", "r");
                if (pipe) {
                    char buf[4096];
                    while (std::fgets(buf, sizeof(buf), pipe)) {
                        std::string s(buf);
                        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
                        if (!s.empty()) paths.push_back(s);
                    }
                    ::pclose(pipe);
                } else {
                    needFallback = true;
                }
            }
#else
            needFallback = true;
#endif
            (void)isImport;
            {
                std::lock_guard<std::mutex> lk(filePickerMtx_);
                filePickerPaths_    = std::move(paths);
                filePickerFallback_ = needFallback;
            }
            filePickerDone_.store(true, std::memory_order_release);
        });
    }

    void showStatus(const char* msg)
    {
        statusMsg_   = msg;
        statusMsgAt_ = std::chrono::steady_clock::now();
        repaint();
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
            e.pan           = z.pan;
            e.muted         = z.muted;
            e.octaveShift   = z.octaveShift;
            zones_.push_back(std::move(e));
        }
        // Auto-select first zone when none is selected (e.g. after project reload)
        if (selectedZone_ < 0 && !zones_.empty())
            selectedZone_ = 0;
        // Reload waveform peaks if selected zone is still valid
        if (selectedZone_ >= 0 && selectedZone_ < static_cast<int>(zones_.size()))
            loadPeaks(selectedZone_);
        char sb[48];
        std::snprintf(sb, sizeof(sb), "Loaded %d zone%s",
                      static_cast<int>(zones_.size()), zones_.size() == 1 ? "" : "s");
        showStatus(sb);
    }

    void confirmDialog()
    {
        switch (dialogMode_) {
        case kDialogSavePatch:
            if (!dialogText_.empty()) {
                setState(kStateKeyPatchSave, dialogText_.c_str());
                showStatus("patch saved");
            }
            break;
        case kDialogSettings:
            if (!dialogText_.empty()) {
                dataDir_ = dialogText_;
                setState(kStateKeyDataDir, dialogText_.c_str());
            }
            break;
        case kDialogDrumReport: {
            // DSP already applied assignments; pushZoneUpdate confirms UI/DSP sync.
            int assigned = 0;
            for (std::size_t zi = 0; zi < zones_.size() && zi < drumReportAssignments_.size(); ++zi) {
                const auto& a = drumReportAssignments_[zi];
                if (a.gmNote >= 0) ++assigned;
                pushZoneUpdate(zi);
            }
            char sb[64];
            std::snprintf(sb, sizeof(sb), "Applied %d/%d drum assignments",
                          assigned, static_cast<int>(zones_.size()));
            showStatus(sb);
            drumReportAssignments_.clear();
            preDrumMapZones_.clear();
            break;
        }
        default: break;
        }
        dialogMode_ = kDialogNone;
        repaint();
    }

    void drawDrumReportDialog(float W, float H)
    {
        constexpr float kRptRowH  = 22.0f;
        constexpr float kRptBodyH = 280.0f;
        constexpr float kRptPw    = 560.0f;
        constexpr float kBtnH     = 28.0f;
        const float kRptPh = 56.0f + kRptBodyH + 14.0f + kBtnH + 14.0f;

        // Dim background
        beginPath();
        fillColor(0, 0, 0, 180);
        rect(0.0f, 0.0f, W, H);
        fill();
        closePath();

        const float dlgX = (W - kRptPw) * 0.5f;
        const float dlgY = (H - kRptPh) * 0.5f;

        // Panel
        beginPath();
        fillColor(22, 32, 44, 255);
        roundedRect(dlgX, dlgY, kRptPw, kRptPh, 10.0f);
        fill();
        closePath();
        beginPath();
        strokeColor(72, 108, 136, 255);
        strokeWidth(1.5f);
        roundedRect(dlgX, dlgY, kRptPw, kRptPh, 10.0f);
        stroke();
        closePath();

        // Title
        const int nAssigned = static_cast<int>(
            std::count_if(drumReportAssignments_.begin(), drumReportAssignments_.end(),
                          [](const downspout::campione::DrumAssignment& a){ return a.gmNote >= 0; }));
        char title[80];
        std::snprintf(title, sizeof(title), "Drum Map — %d/%d zones matched",
                      nAssigned, static_cast<int>(drumReportAssignments_.size()));
        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(220, 232, 242, 255);
        text(dlgX + 18.0f, dlgY + 14.0f, title, nullptr);

        // Column headers
        const float hdrY = dlgY + 36.0f;
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(100, 140, 170, 255);
        text(dlgX + 18.0f,        hdrY, "Zone", nullptr);
        text(dlgX + 18.0f + 300.0f, hdrY, "GM Note", nullptr);
        text(dlgX + 18.0f + 390.0f, hdrY, "Conf", nullptr);
        text(dlgX + 18.0f + 450.0f, hdrY, "Evidence", nullptr);

        // Separator line
        beginPath();
        strokeColor(52, 78, 100, 255);
        strokeWidth(1.0f);
        moveTo(dlgX + 14.0f, hdrY + 14.0f);
        lineTo(dlgX + kRptPw - 14.0f, hdrY + 14.0f);
        stroke();
        closePath();

        // Rows (clipped to body area)
        const float bodyY = hdrY + 18.0f;
        save();
        scissor(dlgX + 4.0f, bodyY, kRptPw - 8.0f, kRptBodyH);

        const int n = static_cast<int>(drumReportAssignments_.size());
        for (int i = drumReportScrollOffset_; i < n; ++i) {
            const float ry = bodyY + static_cast<float>(i - drumReportScrollOffset_) * kRptRowH;
            if (ry > bodyY + kRptBodyH) break;

            const auto& a = drumReportAssignments_[static_cast<std::size_t>(i)];
            const bool matched = (a.gmNote >= 0);

            // Alternating row shade
            if (i % 2 == 0) {
                beginPath();
                fillColor(28, 40, 54, 180);
                rect(dlgX + 14.0f, ry, kRptPw - 28.0f, kRptRowH);
                fill();
                closePath();
            }

            // Zone filename (truncated)
            const std::string fname = basename(zones_.size() > static_cast<std::size_t>(i)
                                               ? zones_[static_cast<std::size_t>(i)].path : "");
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(matched ? 200 : 110, matched ? 218 : 130, matched ? 232 : 148, 255);
            {
                // Clip filename to 295px
                const float maxFw = 290.0f;
                Rectangle<float> tb;
                textBounds(0.0f, 0.0f, fname.c_str(), nullptr, tb);
                if (tb.getWidth() <= maxFw) {
                    text(dlgX + 18.0f, ry + kRptRowH * 0.5f, fname.c_str(), nullptr);
                } else {
                    // Walk from end until it fits with "…"
                    std::string trimmed = fname;
                    while (trimmed.size() > 1) {
                        trimmed.pop_back();
                        const std::string candidate = trimmed + "\xe2\x80\xa6";
                        textBounds(0.0f, 0.0f, candidate.c_str(), nullptr, tb);
                        if (tb.getWidth() <= maxFw) {
                            text(dlgX + 18.0f, ry + kRptRowH * 0.5f, candidate.c_str(), nullptr);
                            break;
                        }
                    }
                }
            }

            // GM note name
            if (matched) {
                fillColor(120, 220, 140, 255);
                text(dlgX + 18.0f + 300.0f, ry + kRptRowH * 0.5f,
                     midiNoteName(a.gmNote).c_str(), nullptr);

                // Confidence bar + value
                const float barX = dlgX + 18.0f + 390.0f;
                const float barW = 46.0f;
                const float barH = 8.0f;
                const float barY2 = ry + kRptRowH * 0.5f - barH * 0.5f;
                beginPath();
                fillColor(30, 55, 40, 255);
                rect(barX, barY2, barW, barH);
                fill();
                closePath();
                beginPath();
                fillColor(60, 180, 90, 220);
                rect(barX, barY2, barW * std::min(a.confidence, 1.0f), barH);
                fill();
                closePath();
                char confBuf[8];
                std::snprintf(confBuf, sizeof(confBuf), "%.0f%%", a.confidence * 100.0f);
                fontSize(9.5f);
                fillColor(160, 210, 170, 255);
                text(barX + barW + 4.0f, ry + kRptRowH * 0.5f, confBuf, nullptr);

                // Evidence label
                if (!a.evidence.empty()) {
                    fontSize(10.0f);
                    fillColor(100, 150, 120, 200);
                    text(dlgX + 18.0f + 450.0f, ry + kRptRowH * 0.5f,
                         a.evidence.c_str(), nullptr);
                }
            } else {
                fillColor(80, 90, 100, 255);
                text(dlgX + 18.0f + 300.0f, ry + kRptRowH * 0.5f, "—", nullptr);
                fillColor(70, 80, 90, 200);
                fontSize(10.0f);
                text(dlgX + 18.0f + 390.0f, ry + kRptRowH * 0.5f, "no match", nullptr);
            }
        }
        restore();

        // Bottom separator
        const float sepY = bodyY + kRptBodyH + 6.0f;
        beginPath();
        strokeColor(52, 78, 100, 255);
        strokeWidth(1.0f);
        moveTo(dlgX + 14.0f, sepY);
        lineTo(dlgX + kRptPw - 14.0f, sepY);
        stroke();
        closePath();

        // Buttons
        const float btnY = sepY + 8.0f;
        const float btnW = 86.0f;
        drumReportOkBtn_     = { dlgX + kRptPw - btnW * 2.0f - 26.0f, btnY, btnW, kBtnH };
        drumReportCancelBtn_ = { dlgX + kRptPw - btnW - 14.0f,         btnY, btnW, kBtnH };

        // Map to shared dialog button rects so the existing click handler works
        dialogOkBtn_     = drumReportOkBtn_;
        dialogCancelBtn_ = drumReportCancelBtn_;

        drawButton(drumReportOkBtn_,     "Apply",  36, 90, 54);
        drawButton(drumReportCancelBtn_, "Cancel", 55, 55, 62);

        // Scroll hint
        if (n > static_cast<int>(kRptBodyH / kRptRowH)) {
            fontSize(9.5f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(72, 95, 112, 200);
            text(dlgX + 18.0f, btnY + kBtnH * 0.5f, "Scroll to see all zones", nullptr);
        }
    }

    void drawDialog(float W, float H)
    {
        // Dim background
        beginPath();
        fillColor(0, 0, 0, 180);
        rect(0.0f, 0.0f, W, H);
        fill();
        closePath();

        const float pw  = 500.0f;
        const float ph  = (dialogMode_ == kDialogSettings) ? 170.0f : 140.0f;
        const float dlgX = (W - pw) * 0.5f;
        const float dlgY = (H - ph) * 0.5f;

        // Panel background
        beginPath();
        fillColor(22, 32, 44, 255);
        roundedRect(dlgX, dlgY, pw, ph, 10.0f);
        fill();
        closePath();
        beginPath();
        strokeColor(72, 108, 136, 255);
        strokeWidth(1.5f);
        roundedRect(dlgX, dlgY, pw, ph, 10.0f);
        stroke();
        closePath();

        // Title
        const char* title = (dialogMode_ == kDialogSavePatch) ? "Save Patch" : "Settings";
        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(220, 232, 242, 255);
        text(dlgX + 18.0f, dlgY + 14.0f, title, nullptr);

        float fieldY = dlgY + 48.0f;

        // Settings: extra note
        if (dialogMode_ == kDialogSettings) {
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(110, 140, 162, 255);
            text(dlgX + 18.0f, dlgY + 36.0f, "Data directory (recordings, patches, slices):", nullptr);
            fieldY = dlgY + 60.0f;
        } else {
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(140, 162, 178, 255);
            text(dlgX + 18.0f, dlgY + 36.0f, "Save to:", nullptr);
        }

        // Text field
        dialogTextField_ = { dlgX + 18.0f, fieldY, pw - 36.0f, 26.0f };
        beginPath();
        fillColor(12, 18, 26, 255);
        roundedRect(dialogTextField_.x, dialogTextField_.y,
                    dialogTextField_.w, dialogTextField_.h, 4.0f);
        fill();
        closePath();
        beginPath();
        strokeColor(82, 128, 162, 255);
        strokeWidth(1.0f);
        roundedRect(dialogTextField_.x, dialogTextField_.y,
                    dialogTextField_.w, dialogTextField_.h, 4.0f);
        stroke();
        closePath();

        // Text content (clipped, scroll to show end)
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        {
            const float maxW = dialogTextField_.w - 12.0f;
            Rectangle<float> twBounds;
            const float tw = textBounds(0.0f, 0.0f, dialogText_.c_str(), nullptr, twBounds);
            float textX = dialogTextField_.x + 6.0f;
            if (tw > maxW) textX = dialogTextField_.x + 6.0f + maxW - tw;

            save();
            scissor(dialogTextField_.x + 2.0f, dialogTextField_.y,
                    dialogTextField_.w - 4.0f, dialogTextField_.h);
            fillColor(210, 222, 232, 255);
            text(textX, dialogTextField_.y + dialogTextField_.h * 0.5f,
                 dialogText_.c_str(), nullptr);

            // Blinking cursor (500ms period)
            const bool cursorOn = (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() / 500) % 2 == 0;
            if (cursorOn) {
                const float cx = std::min(textX + tw + 1.0f,
                                          dialogTextField_.x + dialogTextField_.w - 4.0f);
                beginPath();
                fillColor(200, 220, 240, 200);
                rect(cx, dialogTextField_.y + 4.0f, 1.5f, dialogTextField_.h - 8.0f);
                fill();
                closePath();
            }
            restore();
        }

        // Buttons
        const float btnW = 86.0f, btnH = 28.0f;
        const float btnY = dlgY + ph - btnH - 14.0f;
        dialogOkBtn_     = { dlgX + pw - btnW * 2.0f - 26.0f, btnY, btnW, btnH };
        dialogCancelBtn_ = { dlgX + pw - btnW - 14.0f,         btnY, btnW, btnH };

        const char* okLabel = (dialogMode_ == kDialogSavePatch) ? "Save" : "Apply";
        drawButton(dialogOkBtn_,     okLabel,  36, 90, 54);
        drawButton(dialogCancelBtn_, "Cancel", 55, 55, 62);

        // Keyboard hint
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(72, 95, 112, 200);
        text(dlgX + 18.0f, btnY + btnH * 0.5f, "Enter to confirm \xe2\x80\xa2  Esc to cancel", nullptr);
    }

    std::array<float, kParameterCount> values_ {};
    std::vector<ZoneEntry>             zones_;

    Rect loadBtn_       {};
    Rect recBtn_        {};
    Rect savePatchBtn_  {};
    Rect loadPatchBtn_  {};
    Rect settingsBtn_   {};
    Rect mcpBtn_        {};
    Rect chBtn_         {};
    Rect deleteAllBtn_  {};
    // Context menu
    float contextMenuX_ = 0.0f;
    float contextMenuY_ = 0.0f;
    // Dialog
    DialogMode  dialogMode_      = kDialogNone;
    std::string dialogText_;
    Rect        dialogTextField_ {};
    Rect        dialogOkBtn_     {};
    Rect        dialogCancelBtn_ {};
    // Drum report dialog
    std::vector<downspout::campione::DrumAssignment> drumReportAssignments_;
    std::vector<ZoneEntry>                           preDrumMapZones_;
    bool pendingDrumDialog_      = false;
    int  drumReportScrollOffset_ = 0;
    Rect drumReportOkBtn_        {};
    Rect drumReportCancelBtn_    {};
    std::string dataDir_;
    std::string fileBrowserKey_;  // state key to route the next uiFileBrowserSelected result
    Rect volSlider_ {};
    Rect waveRect_       {};
    Rect loopStartHandle_{};
    Rect loopEndHandle_  {};
    // Wave edit action buttons (built during draw, hit-tested in onMouse)
    Rect waveNormBtn_  {};
    Rect waveTrimBtn_  {};
    Rect waveFadeBtn_  {};
    Rect waveRevBtn_   {};
    Rect waveRandBtn_  {};
    // Flash state: which action button was last clicked and when
    enum { kFlashNone=0, kFlashNorm, kFlashTrim, kFlashFade, kFlashRev, kFlashRand };
    int flashedBtn_ = kFlashNone;
    std::chrono::steady_clock::time_point flashedBtnAt_;
    Rect waveSliceBtn_     {};
    Rect waveSliceDecBtn_  {};
    Rect waveSliceCountRect_{};
    Rect waveSliceIncBtn_  {};
    Rect drumBtn_          {};
    Rect spreadBtn_        {};
    Rect importBtn_        {};
    int  sliceCount_ = 0;   // 0 = auto-detect, >0 = explicit slice count
    // ADSR/filter drag sliders (built during draw, hit-tested in onMouse)
    Rect adsrAttackSlider_  {};
    Rect adsrDecaySlider_   {};
    Rect adsrSustainSlider_ {};
    Rect adsrReleaseSlider_ {};
    Rect filterEnableBtn_   {};
    Rect filterTypeBtn_     {};
    Rect filterCutoffSlider_{};
    Rect filterQSlider_     {};
    Rect panSlider_         {};
    // For ADSR/pan float drags: track start value during drag
    float dragStartFloat_ = 0.0f;

    DragField dragField_        = kDragNone;
    int       dragZoneIdx_      = -1;
    float     dragStartY_       = 0.0f;
    float     dragStartX_       = 0.0f;
    int       dragStartNote_    = 0;
    uint32_t  dragStartFrame_   = 0;
    // Zone reorder drag state
    int       reorderFromIdx_   = -1;   // zone being dragged
    int       reorderTargetIdx_ = -1;   // insertion position (0..zones_.size())
    // Multi-file picker (zenity background thread)
    std::thread              filePickerThread_;
    std::atomic<bool>        filePickerDone_     {true};
    std::mutex               filePickerMtx_;
    std::vector<std::string> filePickerPaths_;
    bool                     filePickerFallback_ = false;
    bool                     filePickerIsImport_ = false;

    int              selectedZone_   = -1;
    int              previewZoneIdx_ = -1;  // zone currently playing via Play button (-1 = none)
    std::vector<float> peaks_;
    int              peakZoneIdx_    = -1;
    bool             peakLoadFailed_ = false;
    // Async peak loading state
    std::thread              peakThread_;
    std::atomic<bool>        peakThreadDone_   {true};
    int                      peakPendingIdx_   {-1};
    std::mutex               peakResultMtx_;
    PeakResult               peakThreadResult_;
    // Rate-limiter for animation repaints (recording timer, patch-saved flash, dialog)
    std::chrono::steady_clock::time_point lastAnimRepaint_ {};

    bool recording_ = false;
    std::chrono::steady_clock::time_point recordStart_;

    bool patchSaved_ = false;
    std::chrono::steady_clock::time_point patchSavedAt_;

    std::string statusMsg_;
    std::chrono::steady_clock::time_point statusMsgAt_;

    bool     dataDirSent_      = false;
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
