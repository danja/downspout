#include "downspout/dpf/MagnetoKit.hpp"

#include "fontana_params.hpp"
#include "fontana_sample_loader.hpp"
#include "fontana_serialization.hpp"
#include "fontana_slices.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::fontana;

enum LocalUiIndex : std::uint32_t {
    kLocalSliceCount = kParameterCount,
    kLocalSensitivity,
    kLocalUiCount
};

enum ActionId {
    kActionBrowserUp = 0,
    kActionBrowserHome,
    kActionBrowserScrollUp,
    kActionBrowserScrollDown,
    kActionLoadFile,
    kActionAudition,
    kActionEqualSlices,
    kActionTransientSlices,
    kActionNormalizeSlices,
    kActionToggleMute,
    kActionPrevSlice,
    kActionNextSlice,
    kActionClearSlices,
};

constexpr kit::Accent kAccentBrowser {88, 125, 154};
constexpr kit::Accent kAccentWave {78, 166, 169};
constexpr kit::Accent kAccentSlices {205, 151, 79};
constexpr kit::Accent kAccentPlayer {193, 116, 105};

constexpr std::array<kit::Range, kLocalUiCount> kRanges = {{
    {0.0f, 1.0f, false},        // master volume
    {0.0f, 16.0f, true},        // midi channel
    {0.0f, 127.0f, true},       // bank
    {0.0f, 127.0f, true},       // program
    {-24.0f, 24.0f, true},      // transpose
    {0.0f, 24.0f, true},        // bend range
    {0.0f, 2000.0f, true},      // attack
    {0.0f, 5000.0f, true},      // release
    {0.0f, 1.0f, true},         // source mode
    {0.0f, 108.0f, true},       // slice base note
    {0.0f, 1.0f, true},         // slice loop
    {0.0f, 1.0f, true},         // filter enabled
    {100.0f, 20000.0f, false, true}, // filter cutoff
    {-1.0f, 1.0f, false},       // pan
    {-1.0f, 127.0f, true},      // status bank
    {-1.0f, 127.0f, true},      // status program
    {0.0f, 32.0f, true},        // status voices
    {-1.0f, 127.0f, true},      // status last note
    {0.0f, 3.0f, true},         // status load
    {2.0f, 64.0f, true},        // local slice count
    {0.0f, 1.0f, false},        // local transient sensitivity
}};

constexpr std::array<float, kLocalUiCount> kDefaults = {{
    0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 4.0f, 120.0f,
    0.0f, 36.0f, 0.0f, 0.0f, 20000.0f,
    0.0f,
    -1.0f, -1.0f, 0.0f, -1.0f, 0.0f,
    8.0f, 0.5f,
}};

struct BrowserEntry {
    std::string name;
    std::string path;
    bool directory = false;
};

[[nodiscard]] bool hasSupportedExtension(const std::string& name)
{
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".wav" || ext == ".ogg" || ext == ".sf2" || ext == ".sf3";
}

[[nodiscard]] bool isSoundFontPathName(const std::string& path)
{
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".sf2" || ext == ".sf3";
}

[[nodiscard]] std::string shortText(const std::string& text, const std::size_t limit)
{
    if (text.size() <= limit)
        return text;
    return text.substr(0, limit > 7 ? 4 : 1) + "..." + text.substr(text.size() - (limit > 7 ? limit - 7 : 1));
}

[[nodiscard]] std::string initialDirectory()
{
    if (const char* home = std::getenv("HOME"))
        return std::string(home);
    std::error_code ec;
    return std::filesystem::current_path(ec).string();
}

[[nodiscard]] std::string formatPan(const float value)
{
    if (std::fabs(value) < 0.02f)
        return "Center";
    const int amount = static_cast<int>(std::lround(std::fabs(value) * 100.0f));
    return (value < 0.0f ? "L" : "R") + std::to_string(amount);
}

[[nodiscard]] std::string formatHz(const float value)
{
    char buf[32];
    if (value >= 1000.0f)
        std::snprintf(buf, sizeof(buf), "%.1f kHz", value / 1000.0f);
    else
        std::snprintf(buf, sizeof(buf), "%.0f Hz", value);
    return buf;
}

[[nodiscard]] std::string formatStatus(const float status)
{
    switch (static_cast<int>(status)) {
    case 1: return "LOADING";
    case 2: return "OK";
    case 3: return "ERROR";
    default: return "IDLE";
    }
}

[[nodiscard]] std::string describeSelection(const SampleSource& source)
{
    return describeSource(source);
}

}  // namespace

class FontanaUI : public MagnetoUI
{
public:
    FontanaUI()
        : MagnetoUI(kRanges.data(), kLocalUiCount, kDefaults.data())
    {
        browserDir_ = initialDirectory();
        refreshBrowser();
    }

protected:
    void onAction(const int id) override
    {
        switch (id) {
        case kActionBrowserUp: {
            std::error_code ec;
            const std::filesystem::path parent = std::filesystem::path(browserDir_).parent_path();
            if (!parent.empty())
                navigateBrowser(parent.string());
            else
                refreshBrowser();
            break;
        }
        case kActionBrowserHome:
            navigateBrowser(initialDirectory());
            break;
        case kActionBrowserScrollUp:
            scrollBrowser(-4);
            break;
        case kActionBrowserScrollDown:
            scrollBrowser(4);
            break;
        case kActionLoadFile:
            assignSelectedFile();
            break;
        case kActionAudition:
            auditionSelectedFile();
            break;
        case kActionEqualSlices:
            generateEqualSlices();
            break;
        case kActionTransientSlices:
            generateTransientSlices();
            break;
        case kActionNormalizeSlices:
            normalizeCurrentSlices();
            break;
        case kActionToggleMute:
            if (selectedSlice_ >= 0 && selectedSlice_ < static_cast<int>(slices_.slices.size())) {
                slices_.slices[static_cast<std::size_t>(selectedSlice_)].muted =
                    !slices_.slices[static_cast<std::size_t>(selectedSlice_)].muted;
                commitSlices();
            }
            break;
        case kActionPrevSlice:
            selectSlice(std::max(0, selectedSlice_ - 1));
            break;
        case kActionNextSlice:
            selectSlice(std::min(static_cast<int>(slices_.slices.size()) - 1, selectedSlice_ + 1));
            break;
        case kActionClearSlices:
            slices_ = SliceSet {};
            selectedSlice_ = -1;
            commitSlices();
            break;
        }
    }

    void sendValue(const std::uint32_t parameter, const float v) override
    {
        if (parameter >= kParameterCount) {
            repaint();
            return;
        }
        setParameterValue(parameter, v);
        setState(kStateKeySettings, serializeSettings(parametersFromUi()).c_str());
    }

    void stateChanged(const char* key, const char* value) override
    {
        const std::string text = value != nullptr ? value : "";
        if (std::strcmp(key, kStateKeySettings) == 0) {
            const auto restored = deserializeSettings(text);
            if (restored.has_value()) {
                setValue(kParamMasterVolume, restored->masterVolume);
                setValue(kParamBank, restored->bank);
                setValue(kParamProgram, restored->program);
                setValue(kParamBendRange, restored->pitchBendRange);
                setValue(kParamTranspose, restored->transpose);
                setValue(kParamSliceBaseNote, restored->sliceBaseNote);
                setValue(kParamPan, restored->pan);
            }
            return;
        }
        if (std::strcmp(key, kStateKeySoundFontPath) == 0) {
            soundFontPath_ = text;
            repaint();
            return;
        }
        if (std::strcmp(key, kStateKeySamplePath) == 0) {
            samplePath_ = text;
            repaint();
            return;
        }
        if (std::strcmp(key, kStateKeySliceMap) == 0) {
            const auto restored = deserializeSliceSet(text);
            if (restored.has_value()) {
                slices_ = *restored;
                selectedSlice_ = slices_.empty() ? -1 : 0;
            }
            repaint();
            return;
        }
        if (std::strcmp(key, kStateKeyPreviewPath) == 0) {
            previewPath_ = text;
            repaint();
            return;
        }
        if (std::strcmp(key, kStateKeyBrowserDir) == 0) {
            // An empty value is a fresh project: keep the directory chosen by the constructor.
            if (text.empty())
                return;
            browserDir_ = text;
            refreshBrowser();
        }
    }

    void uiIdle() override
    {
        if (statusPulse_ > 0) {
            --statusPulse_;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("FONTANA", "SoundFont player + slice sampler",
                   "Browse, audition, then load an SF2/SF3 bank or a WAV/Ogg sample. Click waveform boundaries to edit slices.");

        const float pad = 20.0f;
        const float top = 108.0f;
        const float bottom = getHeight() - pad;
        const float browserW = 360.0f;
        const float waveX = pad + browserW + 16.0f;
        const float rightW = 330.0f;
        const float waveW = getWidth() - waveX - rightW - 16.0f - pad;

        drawBrowser(pad, top, browserW, bottom - top);
        drawWaveform(waveX, top, waveW, 430.0f);
        drawSliceControls(waveX, top + 446.0f, waveW, bottom - top - 446.0f);
        drawPlayer(getWidth() - rightW - pad, top, rightW, bottom - top);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (!ev.press && draggingScrollbar_) {
            draggingScrollbar_ = false;
            return true;
        }
        if (!ev.press && draggingBoundary_ >= 0) {
            draggingBoundary_ = -1;
            commitSlices();
            return true;
        }

        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        if (ev.press && ev.button == 1) {
            if (browserScrollThumbRect_.contains(mx, my)) {
                draggingScrollbar_ = true;
                scrollbarGrabOffset_ = my - browserScrollThumbRect_.y;
                return true;
            }
            if (browserScrollTrackRect_.contains(mx, my)) {
                draggingScrollbar_ = true;
                scrollbarGrabOffset_ = browserScrollThumbRect_.h * 0.5f;
                setBrowserScrollFromPointer(my);
                return true;
            }
            for (std::size_t i = 0; i < browserRowRects_.size(); ++i) {
                if (!browserRowRects_[i].contains(mx, my))
                    continue;
                const std::size_t index = static_cast<std::size_t>(browserScroll_) + i;
                if (index >= browserEntries_.size())
                    return false;
                selectedEntry_ = static_cast<int>(index);
                if (browserEntries_[index].directory) {
                    navigateBrowser(browserEntries_[index].path);
                } else {
                    loadPreviewForPath(browserEntries_[index].path);
                }
                return true;
            }
        }

        const bool consumedByKit = MagnetoUI::onMouse(ev);
        if (consumedByKit)
            return true;
        if (ev.button != 1)
            return false;

        if (ev.press) {
            if (wavePlot_.contains(mx, my) && !slices_.empty()) {
                const int boundary = boundaryAtPoint(mx);
                if (boundary >= 0) {
                    draggingBoundary_ = boundary;
                    return true;
                }
                selectSlice(sliceAtPoint(mx));
                return true;
            }
            return false;
        }

        draggingBoundary_ = -1;
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        if (!browserListRect_.contains(mx, my) && !browserScrollTrackRect_.contains(mx, my))
            return false;
        const double delta = ev.delta.getY();
        if (delta == 0.0)
            return true;
        scrollBrowser(delta > 0.0 ? -3 : 3);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (draggingScrollbar_) {
            setBrowserScrollFromPointer(static_cast<float>(ev.pos.getY()));
            return true;
        }
        if (MagnetoUI::onMotion(ev))
            return true;
        if (draggingBoundary_ <= 0 || slices_.empty())
            return false;
        const float mx = static_cast<float>(ev.pos.getX());
        if (!wavePlot_.contains(mx, wavePlot_.y + 10.0f))
            return false;
        const std::uint32_t frame = frameAtPoint(mx);
        Slice& left = slices_.slices[static_cast<std::size_t>(draggingBoundary_ - 1)];
        Slice& right = slices_.slices[static_cast<std::size_t>(draggingBoundary_)];
        const std::uint32_t minFrame = left.start + 1u;
        const std::uint32_t maxFrame = right.end > minFrame ? right.end - 1u : minFrame;
        const std::uint32_t clamped = std::max(minFrame, std::min(frame, maxFrame));
        left.end = clamped;
        right.start = clamped + 1u;
        repaint();
        return true;
    }

private:
    [[nodiscard]] Parameters parametersFromUi() const
    {
        Parameters p;
        p.masterVolume = value(kParamMasterVolume);
        p.midiChannel = value(kParamMidiChannel);
        p.bank = value(kParamBank);
        p.program = value(kParamProgram);
        p.transpose = value(kParamTranspose);
        p.pitchBendRange = value(kParamBendRange);
        p.attackMs = value(kParamAttack);
        p.releaseMs = value(kParamRelease);
        p.sourceMode = value(kParamSourceMode);
        p.sliceBaseNote = value(kParamSliceBaseNote);
        p.sliceLoop = value(kParamSliceLoop);
        p.filterEnabled = value(kParamFilterEnabled);
        p.filterCutoff = value(kParamFilterCutoff);
        p.pan = value(kParamPan);
        return p;
    }

    [[nodiscard]] int maxBrowserScroll() const noexcept
    {
        return std::max(0, static_cast<int>(browserEntries_.size()) - browserVisibleRows_);
    }

    void scrollBrowser(const int delta)
    {
        browserScroll_ = std::clamp(browserScroll_ + delta, 0, maxBrowserScroll());
        repaint();
    }

    void setBrowserScrollFromPointer(const float pointerY)
    {
        const float travel = browserScrollTrackRect_.h - browserScrollThumbRect_.h;
        if (travel <= 0.0f) {
            browserScroll_ = 0;
            repaint();
            return;
        }
        const float ratio = std::clamp(
            (pointerY - scrollbarGrabOffset_ - browserScrollTrackRect_.y) / travel, 0.0f, 1.0f);
        browserScroll_ = static_cast<int>(ratio * static_cast<float>(maxBrowserScroll()) + 0.5f);
        repaint();
    }

    void navigateBrowser(const std::string& dir)
    {
        browserDir_ = dir;
        // Persist before repainting so the host saves the new folder even if the
        // editor is closed immediately afterwards.
        setState(kStateKeyBrowserDir, browserDir_.c_str());
        refreshBrowser();
    }

    void refreshBrowser()
    {
        browserEntries_.clear();
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(browserDir_, ec)) {
            if (ec)
                break;
            const std::string name = entry.path().filename().string();
            if (!name.empty() && name.front() == '.')
                continue;
            const bool directory = entry.is_directory(ec);
            if (directory || hasSupportedExtension(name)) {
                browserEntries_.push_back({name, entry.path().string(), directory});
            }
        }
        std::sort(browserEntries_.begin(), browserEntries_.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
            if (a.directory != b.directory)
                return a.directory;
            std::string la = a.name;
            std::string lb = b.name;
            std::transform(la.begin(), la.end(), la.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(lb.begin(), lb.end(), lb.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return la < lb;
        });
        selectedEntry_ = browserEntries_.empty() ? -1 : 0;
        browserScroll_ = 0;
        clearPreview();
        previewInfo_ = browserEntries_.empty() ? "No supported files in this folder" : "Select a file to preview";
        statusPulse_ = 30;
        repaint();
    }

    void clearPreview()
    {
        previewPeaks_.clear();
        previewInfo_ = "Select a sound file to see its waveform";
        previewSource_ = SampleSource {};
    }

    void loadPreviewForPath(const std::string& path)
    {
        clearPreview();
        const auto loaded = loadPreviewSource(path);
        if (!loaded.error.empty()) {
            previewInfo_ = loaded.error;
            statusPulse_ = 60;
            repaint();
            return;
        }
        previewSource_ = loaded.source;
        previewPeaks_ = computePeaks(previewSource_, 168);
        previewInfo_ = describeSelection(previewSource_);
        statusPulse_ = 60;
        repaint();
    }

    void auditionSelectedFile()
    {
        if (selectedEntry_ < 0 || selectedEntry_ >= static_cast<int>(browserEntries_.size()))
            return;
        const std::string path = browserEntries_[static_cast<std::size_t>(selectedEntry_)].path;
        setState(kStateKeyPreviewPath, path.c_str());
        ++previewSerial_;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%u", previewSerial_);
        setState(kStateKeyPreviewTrigger, buf);
    }

    void assignSelectedFile()
    {
        if (selectedEntry_ < 0 || selectedEntry_ >= static_cast<int>(browserEntries_.size()))
            return;
        const std::string path = browserEntries_[static_cast<std::size_t>(selectedEntry_)].path;
        if (isSoundFontPathName(path)) {
            setState(kStateKeySoundFontPath, path.c_str());
            soundFontPath_ = path;
            setValue(kParamSourceMode, 0.0f);
            commit(kParamSourceMode, 0.0f);
        } else {
            setState(kStateKeySamplePath, path.c_str());
            samplePath_ = path;
            setValue(kParamSourceMode, 1.0f);
            commit(kParamSourceMode, 1.0f);
            SampleLoadResult loaded = loadSampleSource(path);
            if (loaded.error.empty()) {
                SliceSet fresh = equalSlices(loaded.source, static_cast<int>(value(kLocalSliceCount)),
                                             static_cast<int>(value(kParamSliceBaseNote)));
                fresh.sourcePath = path;
                fresh.sampleRate = loaded.source.sampleRate;
                fresh.frameCount = loaded.source.frameCount();
                slices_ = fresh;
                selectedSlice_ = 0;
                commitSlices();
            }
        }
        auditionSelectedFile();
    }

    void generateEqualSlices()
    {
        if (samplePath_.empty())
            return;
        const auto loaded = loadSampleSource(samplePath_);
        if (!loaded.error.empty())
            return;
        slices_ = equalSlices(loaded.source, static_cast<int>(value(kLocalSliceCount)),
                              static_cast<int>(value(kParamSliceBaseNote)));
        slices_.sourcePath = samplePath_;
        slices_.sampleRate = loaded.source.sampleRate;
        slices_.frameCount = loaded.source.frameCount();
        selectedSlice_ = 0;
        commitSlices();
    }

    void generateTransientSlices()
    {
        if (samplePath_.empty())
            return;
        const auto loaded = loadSampleSource(samplePath_);
        if (!loaded.error.empty())
            return;
        const float sensitivity = 1.0f - value(kLocalSensitivity);
        slices_ = transientSlices(loaded.source, sensitivity,
                                  static_cast<int>(value(kParamSliceBaseNote)));
        slices_.sourcePath = samplePath_;
        slices_.sampleRate = loaded.source.sampleRate;
        slices_.frameCount = loaded.source.frameCount();
        selectedSlice_ = 0;
        commitSlices();
    }

    void normalizeCurrentSlices()
    {
        if (slices_.empty())
            return;
        slices_ = normalizeSlices(slices_, slices_.frameCount);
        selectedSlice_ = std::min(selectedSlice_, static_cast<int>(slices_.slices.size()) - 1);
        commitSlices();
    }

    void commitSlices()
    {
        setState(kStateKeySliceMap, serializeSliceSet(slices_).c_str());
        statusPulse_ = 30;
        repaint();
    }

    void selectSlice(const int index)
    {
        if (slices_.empty())
            return;
        selectedSlice_ = std::max(0, std::min(index, static_cast<int>(slices_.slices.size()) - 1));
        repaint();
    }

    [[nodiscard]] std::uint32_t frameAtPoint(const float mx) const noexcept
    {
        if (slices_.frameCount == 0 || wavePlot_.w <= 0.0f)
            return 0;
        const float n = std::clamp((mx - wavePlot_.x) / wavePlot_.w, 0.0f, 1.0f);
        return static_cast<std::uint32_t>(n * static_cast<float>(slices_.frameCount - 1u));
    }

    [[nodiscard]] int sliceAtPoint(const float mx) const noexcept
    {
        if (slices_.empty())
            return -1;
        const std::uint32_t frame = frameAtPoint(mx);
        for (std::size_t i = 0; i < slices_.slices.size(); ++i)
            if (frame >= slices_.slices[i].start && frame <= slices_.slices[i].end)
                return static_cast<int>(i);
        return static_cast<int>(slices_.slices.size()) - 1;
    }

    [[nodiscard]] int boundaryAtPoint(const float mx) const noexcept
    {
        if (slices_.empty() || wavePlot_.w <= 0.0f)
            return -1;
        for (std::size_t i = 1; i < slices_.slices.size(); ++i) {
            const float n = static_cast<float>(slices_.slices[i].start) /
                            static_cast<float>(std::max(1u, slices_.frameCount - 1u));
            const float x = wavePlot_.x + n * wavePlot_.w;
            if (std::fabs(mx - x) <= 6.0f)
                return static_cast<int>(i);
        }
        return -1;
    }

    void drawBrowser(const float x, const float y, const float w, const float h)
    {
        drawPanel({x, y, w, h}, "FILE BROWSER", kAccentBrowser, shortText(browserDir_, 32).c_str());
        drawButton(kActionBrowserUp, "UP", {x + w - 112.0f, y + 42.0f, 46.0f, 26.0f}, kAccentBrowser, false);
        drawButton(kActionBrowserHome, "HOME", {x + w - 60.0f, y + 42.0f, 46.0f, 26.0f}, kAccentBrowser, false);

        constexpr float kScrollGutter = 44.0f;
        const Rect list {x + 12.0f, y + 78.0f, w - 24.0f - kScrollGutter, h - 212.0f};
        browserListRect_ = list;
        drawPlotFrame(list);
        const float rowH = 23.0f;
        const int visible = std::max(1, static_cast<int>(list.h / rowH));
        browserVisibleRows_ = visible;
        browserScroll_ = std::clamp(browserScroll_, 0,
                                    std::max(0, static_cast<int>(browserEntries_.size()) - visible));
        browserRowRects_.clear();

        const auto& t = theme();
        for (int i = 0; i < visible; ++i) {
            const std::size_t index = static_cast<std::size_t>(browserScroll_ + i);
            if (index >= browserEntries_.size())
                break;
            const Rect row {list.x + 2.0f, list.y + 2.0f + static_cast<float>(i) * rowH, list.w - 4.0f, rowH - 2.0f};
            browserRowRects_.push_back(row);
            if (static_cast<int>(index) == selectedEntry_) {
                beginPath();
                fc(t.controlTrack);
                roundedRect(row.x, row.y, row.w, row.h, 3.0f);
                fill();
            }
            const BrowserEntry& entry = browserEntries_[index];
            fc(entry.directory ? t.textPrimary : t.textDim);
            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            const std::string prefix = entry.directory ? ">  " : "   ";
            text(row.x + 6.0f, row.y + row.h * 0.5f, (prefix + shortText(entry.name, 32)).c_str(), nullptr);
        }

        const float scrollX = list.x + list.w + 6.0f;
        drawButton(kActionBrowserScrollUp, "^", {scrollX, list.y, 32.0f, 26.0f}, kAccentBrowser, false);
        drawButton(kActionBrowserScrollDown, "v", {scrollX, list.y + list.h - 26.0f, 32.0f, 26.0f}, kAccentBrowser, false);

        const Rect track {scrollX + 4.0f, list.y + 30.0f, 24.0f,
                          std::max(8.0f, list.h - 60.0f)};
        browserScrollTrackRect_ = track;
        const float thumbH = browserEntries_.empty()
                                 ? track.h
                                 : std::clamp(track.h * static_cast<float>(visible) /
                                                  static_cast<float>(browserEntries_.size()),
                                              20.0f, track.h);
        const float travel = track.h - thumbH;
        const int maxScroll = maxBrowserScroll();
        const float thumbY = track.y + (maxScroll > 0
                                            ? travel * static_cast<float>(browserScroll_) /
                                                  static_cast<float>(maxScroll)
                                            : 0.0f);
        const Rect thumb {track.x, thumbY, track.w, thumbH};
        browserScrollThumbRect_ = thumb;
        beginPath();
        fc(t.controlTrack);
        roundedRect(track.x, track.y, track.w, track.h, 3.0f);
        fill();
        beginPath();
        fillColor(kAccentBrowser.r, kAccentBrowser.g, kAccentBrowser.b,
                  static_cast<unsigned char>(maxScroll > 0 ? 220 : 90));
        roundedRect(thumb.x, thumb.y, thumb.w, thumb.h, 3.0f);
        fill();

        const float infoY = y + h - 124.0f;
        label(x + 12.0f, infoY, "SELECTED");
        fc(t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        if (selectedEntry_ >= 0 && selectedEntry_ < static_cast<int>(browserEntries_.size()))
            text(x + 12.0f, infoY + 18.0f, shortText(browserEntries_[static_cast<std::size_t>(selectedEntry_)].path, 42).c_str(), nullptr);
        else
            text(x + 12.0f, infoY + 18.0f, "none", nullptr);

        label(x + 12.0f, infoY + 44.0f, "PREVIEW");
        fc(t.textDim);
        fontSize(11.0f);
        text(x + 12.0f, infoY + 62.0f, shortText(previewInfo_, 44).c_str(), nullptr);

        drawButton(kActionLoadFile, "LOAD FILE", {x + 12.0f, y + h - 42.0f, 160.0f, 30.0f}, kAccentBrowser);
        drawButton(kActionAudition, "AUDITION", {x + 180.0f, y + h - 42.0f, 168.0f, 30.0f}, kAccentWave);
    }

    void drawWaveform(const float x, const float y, const float w, const float h)
    {
        drawPanel({x, y, w, h}, "PREVIEW WAVEFORM", kAccentWave,
                  previewSource_.empty() ? "no file" : shortText(previewSource_.path, 30).c_str());
        const Rect plot {x + 14.0f, y + 46.0f, w - 28.0f, h - 78.0f};
        wavePlot_ = plot;
        drawPlotFrame(plot);

        const auto& t = theme();
        if (previewPeaks_.empty()) {
            fc(t.textDim);
            fontSize(13.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(plot.x + plot.w * 0.5f, plot.y + plot.h * 0.5f, previewInfo_.c_str(), nullptr);
            return;
        }

        const float mid = plot.y + plot.h * 0.5f;
        const float half = plot.h * 0.44f;
        beginPath();
        strokeColor(kAccentWave.r, kAccentWave.g, kAccentWave.b, 210);
        strokeWidth(1.4f);
        for (std::size_t i = 0; i < previewPeaks_.size(); ++i) {
            const float px = plot.x + (static_cast<float>(i) + 0.5f) / static_cast<float>(previewPeaks_.size()) * plot.w;
            const Peak& peak = previewPeaks_[i];
            moveTo(px, mid - peak.max * half);
            lineTo(px, mid - peak.min * half);
        }
        stroke();

        if (!slices_.empty() && slices_.frameCount > 0) {
            for (std::size_t i = 0; i < slices_.slices.size(); ++i) {
                const Slice& slice = slices_.slices[i];
                const float sx = plot.x + static_cast<float>(slice.start) / static_cast<float>(slices_.frameCount) * plot.w;
                const float ex = plot.x + static_cast<float>(slice.end + 1u) / static_cast<float>(slices_.frameCount) * plot.w;
                if (static_cast<int>(i) == selectedSlice_) {
                    beginPath();
                    fillColor(kAccentSlices.r, kAccentSlices.g, kAccentSlices.b, 34);
                    rect(sx, plot.y + 1.0f, std::max(1.0f, ex - sx), plot.h - 2.0f);
                    fill();
                }
                beginPath();
                strokeColor(kAccentSlices.r, kAccentSlices.g, kAccentSlices.b, static_cast<unsigned char>(slice.muted ? 80 : 190));
                strokeWidth(1.0f);
                moveTo(sx, plot.y);
                lineTo(sx, plot.y + plot.h);
                stroke();
                fc(slice.muted ? t.textDim : t.textPrimary);
                fontSize(10.0f);
                textAlign(ALIGN_LEFT | ALIGN_TOP);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(i));
                text(sx + 3.0f, plot.y + 3.0f, buf, nullptr);
            }
        }

        label(x + 14.0f, y + h - 24.0f,
              (std::string("Slices: ") + std::to_string(slices_.slices.size()) +
               "   Selected: " + (selectedSlice_ >= 0 ? std::to_string(selectedSlice_) : "-") +
               "   Drag an orange boundary to move a slice point").c_str(), false, 11.0f);
    }

    void drawSliceControls(const float x, const float y, const float w, const float h)
    {
        drawPanel({x, y, w, h}, "SLICE MAP", kAccentSlices,
                  slices_.empty() ? "none" : (std::to_string(slices_.slices.size()) + " slices").c_str());

        const float colW = (w - 36.0f) / 2.0f;
        drawStepper(kLocalSliceCount, "Equal slices", std::to_string(static_cast<int>(value(kLocalSliceCount))).c_str(),
                    {x + 14.0f, y + 44.0f, colW - 8.0f, 40.0f});
        drawStepper(kParamSliceBaseNote, "Base note", std::to_string(static_cast<int>(value(kParamSliceBaseNote))).c_str(),
                    {x + 22.0f + colW, y + 44.0f, colW - 8.0f, 40.0f});

        drawSlider(kLocalSensitivity, "Transient sensitivity", std::to_string(static_cast<int>(value(kLocalSensitivity) * 100.0f)).c_str(),
                   {x + 14.0f, y + 104.0f, colW - 8.0f, 40.0f}, kAccentSlices);

        const char* loopModes[] = {"One shot", "Loop slice"};
        drawSegments(kParamSliceLoop, loopModes, 2, {x + 22.0f + colW, y + 104.0f, colW - 8.0f, 26.0f}, kAccentSlices);

        drawButton(kActionEqualSlices, "EQUAL", {x + 14.0f, y + 160.0f, (w - 52.0f) / 4.0f, 30.0f}, kAccentSlices);
        drawButton(kActionTransientSlices, "TRANSIENTS", {x + 22.0f + (w - 52.0f) / 4.0f, y + 160.0f, (w - 52.0f) / 4.0f, 30.0f}, kAccentSlices);
        drawButton(kActionNormalizeSlices, "NORMALIZE", {x + 30.0f + (w - 52.0f) / 2.0f, y + 160.0f, (w - 52.0f) / 4.0f, 30.0f}, kAccentSlices);
        drawButton(kActionClearSlices, "CLEAR", {x + 38.0f + (w - 52.0f) * 3.0f / 4.0f, y + 160.0f, (w - 52.0f) / 4.0f, 30.0f}, kAccentSlices, false);

        drawButton(kActionPrevSlice, "< SLICE", {x + 14.0f, y + 200.0f, 110.0f, 28.0f}, kAccentSlices, false);
        drawButton(kActionNextSlice, "SLICE >", {x + 132.0f, y + 200.0f, 110.0f, 28.0f}, kAccentSlices, false);
        drawButton(kActionToggleMute, "MUTE", {x + 250.0f, y + 200.0f, 90.0f, 28.0f}, kAccentSlices, false);

        if (!slices_.empty() && selectedSlice_ >= 0) {
            const Slice& slice = slices_.slices[static_cast<std::size_t>(selectedSlice_)];
            char buf[128];
            std::snprintf(buf, sizeof(buf), "slice %d: frames %u..%u  note %d  %s",
                          selectedSlice_, slice.start, slice.end, slice.rootNote,
                          slice.muted ? "MUTED" : "active");
            label(x + 14.0f, y + h - 22.0f, buf, false, 11.0f);
        }
    }

    void drawPlayer(const float x, const float y, const float w, const float h)
    {
        drawPanel({x, y, w, h}, "PLAYER", kAccentPlayer);
        const char* sources[] = {"SoundFont", "Slices"};
        drawSegments(kParamSourceMode, sources, 2, {x + 14.0f, y + 44.0f, w - 28.0f, 28.0f}, kAccentPlayer);

        drawStepper(kParamBank, "Bank", std::to_string(static_cast<int>(value(kParamBank))).c_str(),
                    {x + 14.0f, y + 88.0f, (w - 36.0f) / 2.0f, 40.0f});
        drawStepper(kParamProgram, "Preset", std::to_string(static_cast<int>(value(kParamProgram))).c_str(),
                    {x + 22.0f + (w - 36.0f) / 2.0f, y + 88.0f, (w - 36.0f) / 2.0f, 40.0f});
        drawStepper(kParamTranspose, "Transpose", (std::to_string(static_cast<int>(value(kParamTranspose))) + " st").c_str(),
                    {x + 14.0f, y + 142.0f, (w - 36.0f) / 2.0f, 40.0f});
        drawStepper(kParamBendRange, "Bend", (std::to_string(static_cast<int>(value(kParamBendRange))) + " st").c_str(),
                    {x + 22.0f + (w - 36.0f) / 2.0f, y + 142.0f, (w - 36.0f) / 2.0f, 40.0f});

        const float mixerW = (w - 36.0f) / 2.0f;
        drawSlider(kParamMasterVolume, "Master", std::to_string(static_cast<int>(value(kParamMasterVolume) * 100.0f)).c_str(),
                   {x + 14.0f, y + 200.0f, mixerW, 38.0f}, kAccentPlayer);
        drawSlider(kParamPan, "Pan", formatPan(value(kParamPan)).c_str(),
                   {x + 22.0f + mixerW, y + 200.0f, mixerW, 38.0f}, kAccentPlayer);
        drawSlider(kParamAttack, "Attack", (std::to_string(static_cast<int>(value(kParamAttack))) + " ms").c_str(),
                   {x + 14.0f, y + 252.0f, w - 28.0f, 38.0f}, kAccentPlayer);
        drawSlider(kParamRelease, "Release", (std::to_string(static_cast<int>(value(kParamRelease))) + " ms").c_str(),
                   {x + 14.0f, y + 304.0f, w - 28.0f, 38.0f}, kAccentPlayer);

        const char* filterModes[] = {"Filter off", "Filter on"};
        drawSegments(kParamFilterEnabled, filterModes, 2, {x + 14.0f, y + 360.0f, w - 28.0f, 26.0f}, kAccentPlayer);
        setDim(value(kParamFilterEnabled) < 0.5f);
        drawSlider(kParamFilterCutoff, "Cutoff", formatHz(value(kParamFilterCutoff)).c_str(),
                   {x + 14.0f, y + 400.0f, w - 28.0f, 38.0f}, kAccentPlayer);
        setDim(false);

        const auto& t = theme();
        const float statusY = y + 458.0f;
        drawPlotFrame({x + 14.0f, statusY, w - 28.0f, h - statusY + y - 14.0f});
        label(x + 26.0f, statusY + 12.0f, "LIVE STATUS", false, 12.0f);
        const int lines[] = {
            static_cast<int>(value(kParamStatusBank)),
            static_cast<int>(value(kParamStatusProgram)),
            static_cast<int>(value(kParamStatusVoices)),
            static_cast<int>(value(kParamStatusLastNote)),
        };
        const char* names[] = {"Effective bank", "Effective preset", "Active voices", "Last note"};
        char buf[96];
        for (int i = 0; i < 4; ++i) {
            label(x + 26.0f, statusY + 38.0f + static_cast<float>(i) * 22.0f, names[i], true, 11.0f);
            if (lines[i] < 0)
                std::snprintf(buf, sizeof(buf), "-");
            else
                std::snprintf(buf, sizeof(buf), "%d", lines[i]);
            fc(t.textPrimary);
            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            text(x + w - 26.0f, statusY + 38.0f + static_cast<float>(i) * 22.0f, buf, nullptr);
        }
        label(x + 26.0f, statusY + 130.0f, "Load state", true, 11.0f);
        fc(t.textPrimary);
        fontSize(11.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(x + w - 26.0f, statusY + 130.0f, formatStatus(value(kParamStatusLoad)).c_str(), nullptr);

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 26.0f, statusY + 154.0f,
             shortText(soundFontPath_.empty() ? samplePath_ : soundFontPath_, 38).c_str(), nullptr);
    }

    std::string browserDir_ {};
    std::vector<BrowserEntry> browserEntries_ {};
    std::vector<Rect> browserRowRects_ {};
    Rect browserListRect_ {};
    Rect browserScrollTrackRect_ {};
    Rect browserScrollThumbRect_ {};
    int browserVisibleRows_ = 1;
    int selectedEntry_ = -1;
    int browserScroll_ = 0;
    bool draggingScrollbar_ = false;
    float scrollbarGrabOffset_ = 0.0f;
    std::string soundFontPath_ {};
    std::string samplePath_ {};
    std::string previewPath_ {};
    std::string previewInfo_ = "Select a sound file to see its waveform";
    SampleSource previewSource_ {};
    std::vector<Peak> previewPeaks_ {};
    SliceSet slices_ {};
    int selectedSlice_ = -1;
    int draggingBoundary_ = -1;
    int statusPulse_ = 0;
    std::uint32_t previewSerial_ = 0;
    Rect wavePlot_ {};
};

UI* createUI()
{
    return new FontanaUI();
}

END_NAMESPACE_DISTRHO
