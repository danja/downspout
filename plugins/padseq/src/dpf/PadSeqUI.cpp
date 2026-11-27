#include "downspout/dpf/MagnetoKit.hpp"

#include "padseq_core.hpp"
#include "padseq_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::padseq;

constexpr kit::Accent kGridAccent {78, 142, 160};
constexpr kit::Accent kControlAccent {126, 150, 108};
constexpr kit::Accent kTopAccent {188, 118, 62};
constexpr kit::Accent kTriggerAccent {148, 96, 168};
constexpr kit::Accent kDangerAccent {178, 72, 72};

const char* const kNoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

std::array<kit::Range, 64> makeRanges()
{
    std::array<kit::Range, 64> ranges {};
    for (std::uint32_t i = 0; i < ranges.size(); ++i)
    {
        const auto r = rangeFor(static_cast<ParamId>(i));
        ranges[i] = {r.minimum, r.maximum, r.integer};
    }
    return ranges;
}

std::array<float, 64> makeDefaults()
{
    std::array<float, 64> defaults {};
    for (std::uint32_t i = 0; i < defaults.size(); ++i)
        defaults[i] = rangeFor(static_cast<ParamId>(i)).defaultValue;
    return defaults;
}

int rowNote(const int scale, const int root, const int view, const int map, const std::size_t note)
{
    if (map == static_cast<int>(NoteMap::gmDrums))
        return kDrumNotes[note];
    return foldMidi(root + scaleStepAt(static_cast<std::size_t>(scale), static_cast<int>(note) + view));
}

void noteLabel(char* buffer, const std::size_t size, const int note)
{
    const int name = ((note % 12) + 12) % 12;
    const int octave = note / 12 - 1;
    std::snprintf(buffer, size, "%s%d", kNoteNames[name], octave);
}

} // namespace

class PadSeqUI : public MagnetoUI
{
public:
    PadSeqUI()
        : MagnetoUI(ranges_.data(), static_cast<std::uint32_t>(ranges_.size()), defaults_.data())
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            allValues_[i] = rangeFor(static_cast<ParamId>(i)).defaultValue;
    }

protected:
    void onAction(int) override
    {
        // All controls are hit-tested directly in onMouse(); the base class only
        // requires this hook for its registered buttons.
    }

    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index < kParameterCount)
        {
            allValues_[index] = value;
            // The processor owns the pattern, so Randomise/Clear change cells
            // without an explicit UI write. Status cells are the DPF bridge for
            // those changes; mirror them onto the editable cell parameters the
            // grid draws from.
            const uint32_t statusBase = static_cast<uint32_t>(ParamId::statusCellStart);
            if (index >= statusBase && index < statusBase + static_cast<uint32_t>(kGridCellCount))
            {
                const uint32_t visible = index - statusBase;
                const int steps = iv(ParamId::steps);
                const int page = iv(ParamId::page);
                const int step = page * kStepsPerPage + static_cast<int>(visible % kGridWidth);
                const std::size_t note = visible / kGridWidth;
                if (step >= 0 && step < steps)
                    allValues_[static_cast<uint32_t>(ParamId::cellStart) +
                               static_cast<uint32_t>(cellIndex(static_cast<std::size_t>(step), note))] = value;
            }
        }
        MagnetoUI::parameterChanged(index, value);
        repaint();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        // LED Out is appended after the status outputs, beyond MagnetoUI's 64
        // mirrored values, so its segments are hit-tested here rather than by
        // the base class's commit() path.
        if (ev.button == 1 && ev.press)
        {
            for (std::size_t i = 0; i < ledOutCount_; ++i)
            {
                if (!ledOutHits_[i].bounds.contains(static_cast<float>(ev.pos.getX()),
                                                   static_cast<float>(ev.pos.getY())))
                    continue;
                sendClamped(ParamId::ledTransport, static_cast<float>(ledOutHits_[i].index));
                return true;
            }
        }
        if (MagnetoUI::onMouse(ev))
            return true;
        if (ev.button != 1 || !ev.press)
            return false;

        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());

        for (std::size_t i = 0; i < topCount_; ++i)
        {
            if (topHits_[i].bounds.contains(mx, my))
            {
                handleTopButton(i);
                return true;
            }
        }
        for (std::size_t i = 0; i < triggerCount_; ++i)
        {
            if (triggerHits_[i].bounds.contains(mx, my))
            {
                pressedNote_ = static_cast<int>(i);
                sendTrigger(static_cast<ParamId>(static_cast<std::uint32_t>(ParamId::triggerStart) +
                                                 static_cast<std::uint32_t>(i)));
                repaint();
                return true;
            }
        }
        for (std::size_t i = 0; i < gridCount_; ++i)
        {
            if (gridHits_[i].bounds.contains(mx, my))
            {
                const uint32_t parameter = static_cast<uint32_t>(ParamId::cellStart) +
                                           static_cast<uint32_t>(cellIndex(
                                               static_cast<std::size_t>(gridHits_[i].step),
                                               static_cast<std::size_t>(gridHits_[i].note)));
                const float next = allValues_[parameter] >= 0.5f ? 0.0f : 1.0f;
                allValues_[parameter] = next;
                sendValue(parameter, next);
                return true;
            }
        }
        return false;
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("PadSeq", "8x8 step sequencer", "X = time, left to right / Y = note, row 0 at the bottom. Click cells to toggle; side buttons trigger note rows.");

        drawTopStrip();
        drawGridPanel();
        drawControlPanel();
    }

private:
    struct GridHit {
        Rect bounds {};
        int step = 0;
        int note = 0;
    };
    struct IndexedHit {
        Rect bounds {};
        std::size_t index = 0;
    };

    float v(const ParamId id) const
    {
        return allValues_[static_cast<std::uint32_t>(id)];
    }

    int iv(const ParamId id) const
    {
        return static_cast<int>(std::lround(v(id)));
    }

    void sendClamped(const ParamId id, const float value)
    {
        const auto range = rangeFor(id);
        const float clamped = std::clamp(value, range.minimum, range.maximum);
        sendValue(static_cast<std::uint32_t>(id), clamped);
        allValues_[static_cast<std::uint32_t>(id)] = clamped;
        repaint();
    }

    void sendTrigger(const ParamId id)
    {
        sendValue(static_cast<std::uint32_t>(id), 1.0f);
        sendValue(static_cast<std::uint32_t>(id), 0.0f);
    }

    void clearGridValues()
    {
        const std::uint32_t base = static_cast<uint32_t>(ParamId::cellStart);
        for (std::size_t i = 0; i < kCellCount; ++i)
            allValues_[base + static_cast<uint32_t>(i)] = 0.0f;
    }

    void handleTopButton(const std::size_t index)
    {
        switch (static_cast<TopButton>(index))
        {
        case TopButton::viewDown: sendClamped(ParamId::noteView, v(ParamId::noteView) - 1.0f); break;
        case TopButton::viewUp: sendClamped(ParamId::noteView, v(ParamId::noteView) + 1.0f); break;
        case TopButton::pageBack: sendClamped(ParamId::page, v(ParamId::page) - 1.0f); break;
        case TopButton::pageForward: sendClamped(ParamId::page, v(ParamId::page) + 1.0f); break;
        case TopButton::scaleBack: sendClamped(ParamId::scale, v(ParamId::scale) - 1.0f); break;
        case TopButton::scaleForward: sendClamped(ParamId::scale, v(ParamId::scale) + 1.0f); break;
        case TopButton::randomise: sendTrigger(ParamId::randomise); break;
        case TopButton::clear:
            clearGridValues();
            sendTrigger(ParamId::clear);
            break;
        case TopButton::logo:
        case TopButton::count:
            break;
        }
    }

    void drawButtonLike(const int /*id*/, const char* name, const Rect b, const kit::Accent accent,
                        const bool filled = true, const float size = 12.0f)
    {
        const auto& t = theme();
        beginPath();
        if (filled)
            fillColor(accent.r, accent.g, accent.b, 255);
        else
            fc(t.buttonFace);
        roundedRect(b.x, b.y, b.w, b.h, laf::kRadiusSmall);
        fill();
        if (!filled)
        {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            roundedRect(b.x, b.y, b.w, b.h, laf::kRadiusSmall);
            stroke();
        }
        fc(filled ? laf::Colour {250, 248, 242, 255} : t.textPrimary);
        fontSize(size);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(b.x + b.w * 0.5f, b.y + b.h * 0.5f, name, nullptr);
    }

    // Same visual language as MagnetoKit's drawSegments, but reads and writes
    // allValues_ so it can control a parameter beyond the kit's 64-value cap.
    void drawLedOutSegments(const char* const* names, const int count, const Rect b,
                            const kit::Accent accent)
    {
        const auto& t = theme();
        const int selected = iv(ParamId::ledTransport);
        const float gap = 6.0f;
        const float w = (b.w - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        ledOutCount_ = 0;
        for (int i = 0; i < count; ++i)
        {
            const Rect box {b.x + static_cast<float>(i) * (w + gap), b.y, w, b.h};
            beginPath();
            if (i == selected)
                fillColor(accent.r, accent.g, accent.b, 255);
            else
                fc(t.buttonFace);
            roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
            fill();
            if (i != selected)
            {
                beginPath();
                sc(t.border);
                strokeWidth(1.0f);
                roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
                stroke();
            }
            if (i == selected)
                fillColor(250, 248, 242, 255);
            else
                fc(t.textPrimary);
            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, names[i], nullptr);
            if (ledOutCount_ < ledOutHits_.size())
                ledOutHits_[ledOutCount_++] = {box, static_cast<std::size_t>(i)};
        }
    }

    void drawTopStrip()
    {
        const char* const names[] = {
            "91 View-", "92 View+", "93 Page-", "94 Page+", "95 Scale-", "96 Scale+", "97 Random", "98 Clear",
        };
        const kit::Accent accents[] = {
            kGridAccent, kGridAccent, kControlAccent, kControlAccent,
            kTopAccent, kTopAccent, kTriggerAccent, kDangerAccent,
        };
        const Rect panel {24.0f, 108.0f, 932.0f, 72.0f};
        drawPanel(panel, "LAUNCHPAD TOP BUTTONS", kTopAccent, "CC91-CC98");

        const float margin = 12.0f;
        const float gap = 6.0f;
        const float width = (panel.w - margin * 2.0f - gap * 7.0f) / 8.0f;
        topCount_ = 0;
        for (std::size_t i = 0; i < 8; ++i)
        {
            const Rect box {panel.x + margin + static_cast<float>(i) * (width + gap),
                            panel.y + 38.0f, width, 24.0f};
            drawButtonLike(static_cast<int>(i), names[i], box, accents[i], true, 11.0f);
            topHits_[topCount_++] = {box, i};
        }
    }

    void drawGridPanel()
    {
        const Rect panel {24.0f, 188.0f, 620.0f, 502.0f};
        char right[96];
        std::snprintf(right, sizeof(right), "Page %d/%d - steps %d-%d - %s",
                      iv(ParamId::page) + 1, pageCount(iv(ParamId::steps)),
                      iv(ParamId::page) * kStepsPerPage + 1,
                      std::min(iv(ParamId::steps), (iv(ParamId::page) + 1) * kStepsPerPage),
                      iv(ParamId::statusRunning) != 0 ? "RUN" : "STOP");
        drawPanel(panel, "PATTERN GRID", kGridAccent, right);

        const float gridX = 70.0f;
        const float gridY = 236.0f;
        const float cellW = 60.0f;
        const float cellH = 48.0f;
        const float gap = 6.0f;
        const int scale = iv(ParamId::scale);
        const int root = iv(ParamId::root);
        const int view = iv(ParamId::noteView);
        const int map = iv(ParamId::noteMap);
        const int current = iv(ParamId::statusStep);
        const int page = iv(ParamId::page);
        const int steps = iv(ParamId::steps);

        for (std::size_t col = 0; col < kGridWidth; ++col)
        {
            const int step = page * kStepsPerPage + static_cast<int>(col);
            char stepText[16];
            if (step < steps)
                std::snprintf(stepText, sizeof(stepText), "%d", step + 1);
            else
                stepText[0] = '\0';
            label(gridX + static_cast<float>(col) * (cellW + gap) + cellW * 0.5f,
                  232.0f, stepText, false, 10.0f, ALIGN_CENTER | ALIGN_BOTTOM);
        }

        gridCount_ = 0;
        for (std::size_t displayRow = 0; displayRow < kGridHeight; ++displayRow)
        {
            const std::size_t note = kGridHeight - 1 - displayRow;
            const float y = gridY + static_cast<float>(displayRow) * (cellH + gap);
            char noteText[16];
            if (map == static_cast<int>(NoteMap::gmDrums))
                std::snprintf(noteText, sizeof(noteText), "%s", kDrumNames[note]);
            else
                noteLabel(noteText, sizeof(noteText), rowNote(scale, root, view, map, note));
            label(36.0f, y + cellH * 0.5f, noteText, true, 10.0f, ALIGN_RIGHT | ALIGN_MIDDLE);

            for (std::size_t col = 0; col < kGridWidth; ++col)
            {
                const int step = page * kStepsPerPage + static_cast<int>(col);
                const Rect box {gridX + static_cast<float>(col) * (cellW + gap), y, cellW, cellH};
                const bool valid = step < steps;
                const std::uint32_t cellParameter = static_cast<std::uint32_t>(ParamId::cellStart) +
                                                    static_cast<std::uint32_t>(cellIndex(
                                                        static_cast<std::size_t>(step), note));
                const bool active = valid && allValues_[cellParameter] >= 0.5f;
                const bool playing = valid && step == current;

                const auto& t = theme();
                beginPath();
                if (active && playing)
                    fillColor(250, 248, 242, 255);
                else if (active)
                    fillColor(kGridAccent.r, kGridAccent.g, kGridAccent.b, 255);
                else if (playing)
                    fillColor(kTopAccent.r, kTopAccent.g, kTopAccent.b, 110);
                else
                    fc(t.buttonFace);
                roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
                fill();
                beginPath();
                sc(valid ? t.border : t.textDim);
                strokeWidth(1.0f);
                roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
                stroke();

                if (active && !playing)
                {
                    beginPath();
                    fillColor(250, 248, 242, 255);
                    circle(box.x + box.w * 0.5f, box.y + box.h * 0.5f, 5.0f);
                    fill();
                }
                if (valid)
                    gridHits_[gridCount_++] = {box, step, static_cast<int>(note)};
            }
        }

        label(36.0f, 668.0f, "Note", true, 10.0f);
        label(gridX, 668.0f, "Time / steps ->", true, 10.0f);
    }

    void drawControlPanel()
    {
        const Rect panel {660.0f, 188.0f, 296.0f, 502.0f};
        drawPanel(panel, "PATTERN & NOTES", kControlAccent, "CC19-CC89");

        const Rect triggerPanel {672.0f, 226.0f, 272.0f, 128.0f};
        drawPanel(triggerPanel, "SIDE TRIGGERS", kTriggerAccent, "one per note row");
        const float buttonW = 62.0f;
        const float buttonH = 30.0f;
        const float gap = 6.0f;
        const int scale = iv(ParamId::scale);
        const int root = iv(ParamId::root);
        const int view = iv(ParamId::noteView);
        const int map = iv(ParamId::noteMap);
        triggerCount_ = 0;
        for (std::size_t i = 0; i < kGridWidth; ++i)
        {
            const int row = static_cast<int>(i / 4);
            const int col = static_cast<int>(i % 4);
            const Rect box {triggerPanel.x + 12.0f + static_cast<float>(col) * (buttonW + gap),
                            triggerPanel.y + 40.0f + static_cast<float>(row) * (buttonH + gap),
                            buttonW, buttonH};
            char title[20];
            if (map == static_cast<int>(NoteMap::gmDrums))
                std::snprintf(title, sizeof(title), "%d %s", kSideButtonCCs[i], kDrumNames[i]);
            else
            {
                char noteText[8];
                noteLabel(noteText, sizeof(noteText), rowNote(scale, root, view, map, i));
                std::snprintf(title, sizeof(title), "%d %s", kSideButtonCCs[i], noteText);
            }
            drawButtonLike(static_cast<int>(i), title, box, kTriggerAccent,
                           pressedNote_ == static_cast<int>(i), 9.0f);
            triggerHits_[triggerCount_++] = {box, i};
        }
        label(684.0f, 340.0f, "UI buttons and side CCs trigger that note row.", true, 10.0f);

        const char* const clockNames[] = {"Auto", "Host", "Free"};
        const char* const mapNames[] = {"Scale", "GM"};
        const char* const onOff[] = {"Off", "On"};
        const char* const ledOutNames[] = {"Off", "SysEx", "N+CC"};

        std::snprintf(bufSteps_, sizeof(bufSteps_), "%d", iv(ParamId::steps));
        std::snprintf(bufPage_, sizeof(bufPage_), "%d/%d", iv(ParamId::page) + 1,
                      pageCount(iv(ParamId::steps)));
        std::snprintf(bufTempo_, sizeof(bufTempo_), "%.0f BPM", v(ParamId::tempo));
        std::snprintf(bufVelocity_, sizeof(bufVelocity_), "%d", iv(ParamId::velocity));
        std::snprintf(bufScale_, sizeof(bufScale_), "%.0f %s", v(ParamId::scale),
                      kScaleNames[static_cast<std::size_t>(std::clamp(iv(ParamId::scale), 0,
                                                                       static_cast<int>(ScaleId::count) - 1))]);
        std::snprintf(bufRoot_, sizeof(bufRoot_), "%d", iv(ParamId::root));
        std::snprintf(bufView_, sizeof(bufView_), "%+d", iv(ParamId::noteView));
        std::snprintf(bufChannel_, sizeof(bufChannel_), "%d", iv(ParamId::channel));
        std::snprintf(bufSeed_, sizeof(bufSeed_), "%.0f", v(ParamId::seed));

        drawStepper(static_cast<uint32_t>(ParamId::steps), "Steps",
                    bufSteps_, Rect {676.0f, 364.0f, 126.0f, 38.0f});
        drawStepper(static_cast<uint32_t>(ParamId::page), "Page",
                    bufPage_, Rect {814.0f, 364.0f, 126.0f, 38.0f});
        drawSegments(static_cast<uint32_t>(ParamId::clockMode), clockNames, 3,
                     Rect {676.0f, 414.0f, 264.0f, 24.0f}, kControlAccent);

        drawSlider(static_cast<uint32_t>(ParamId::tempo), "Free Tempo",
                   bufTempo_, Rect {676.0f, 450.0f, 126.0f, 30.0f}, kControlAccent);
        drawSlider(static_cast<uint32_t>(ParamId::velocity), "Velocity",
                   bufVelocity_, Rect {814.0f, 450.0f, 126.0f, 30.0f}, kControlAccent);

        drawStepper(static_cast<uint32_t>(ParamId::scale), "Scale",
                    bufScale_, Rect {676.0f, 494.0f, 126.0f, 38.0f});
        drawStepper(static_cast<uint32_t>(ParamId::root), "Root",
                    bufRoot_, Rect {814.0f, 494.0f, 126.0f, 38.0f});
        drawStepper(static_cast<uint32_t>(ParamId::noteView), "Note View",
                    bufView_, Rect {676.0f, 544.0f, 126.0f, 38.0f});
        drawStepper(static_cast<uint32_t>(ParamId::channel), "MIDI Ch",
                    bufChannel_, Rect {814.0f, 544.0f, 126.0f, 38.0f});

        label(676.0f, 586.0f, "Note Map", false, 9.0f);
        label(778.0f, 586.0f, "LED", false, 9.0f);
        label(840.0f, 586.0f, "LED Out", false, 9.0f);
        drawSegments(static_cast<uint32_t>(ParamId::noteMap), mapNames, 2,
                     Rect {676.0f, 598.0f, 96.0f, 22.0f}, kTriggerAccent);
        drawSegments(static_cast<uint32_t>(ParamId::ledFeedback), onOff, 2,
                     Rect {778.0f, 598.0f, 56.0f, 22.0f}, kGridAccent);
        drawLedOutSegments(ledOutNames, 3,
                           Rect {840.0f, 598.0f, 100.0f, 22.0f}, kTopAccent);
        drawSlider(static_cast<uint32_t>(ParamId::seed), "Random Seed",
                   bufSeed_, Rect {676.0f, 628.0f, 264.0f, 30.0f}, kTopAccent);
    }

    std::array<kit::Range, 64> ranges_ = makeRanges();
    std::array<float, 64> defaults_ = makeDefaults();
    std::array<float, kParameterCount> allValues_ {};
    std::array<GridHit, kGridCellCount> gridHits_ {};
    std::array<IndexedHit, 8> topHits_ {};
    std::array<IndexedHit, kGridWidth> triggerHits_ {};
    std::array<IndexedHit, kLedTransportNames.size()> ledOutHits_ {};
    std::size_t gridCount_ = 0;
    std::size_t topCount_ = 0;
    std::size_t triggerCount_ = 0;
    std::size_t ledOutCount_ = 0;
    int pressedNote_ = -1;
    char bufSteps_[16] {};
    char bufPage_[32] {};
    char bufTempo_[24] {};
    char bufVelocity_[16] {};
    char bufScale_[40] {};
    char bufRoot_[16] {};
    char bufView_[16] {};
    char bufChannel_[16] {};
    char bufSeed_[24] {};
};

UI* createUI()
{
    return new PadSeqUI();
}

END_NAMESPACE_DISTRHO
