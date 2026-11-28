#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "energise_core.hpp"
#include "energise_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;
namespace core = downspout::energise;

namespace {

constexpr float kPad = 20.0f;
constexpr float kLeftW = 320.0f;
constexpr float kRightX = 356.0f;
constexpr float kRightW = 744.0f;
constexpr float kPanelTop = 132.0f;
constexpr float kPanelBottom = 700.0f;

constexpr std::size_t idx(const core::ParamId id) noexcept
{
    return static_cast<std::size_t>(id);
}

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

}  // namespace

class EnergiseUI : public UI
{
public:
    EnergiseUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::size_t i = 0; i < core::kParameterCount; ++i)
            values_[i] = core::kParameterSpecs[i].defaultValue;

        #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        #else
        loadSharedResources();
        #endif
    }

protected:
    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index >= core::kParameterCount)
            return;
        values_[index] = value;
        repaint();
    }

    void onNanoDisplay() override
    {
        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());
        drawBackground(w, h);
        drawHeader(w);
        drawTransport(w);
        drawCaptureAndGroove();
        drawOutputSync();
        drawSound();
        drawHelp();
        drawWaveform();
        drawEditStrip();
        drawEventTable();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        getWindow().focus();

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            if (draggingEvent_ >= 0) {
                commitEdit(core::ParamId::editTime, values_[idx(core::ParamId::editTime)]);
                draggingEvent_ = -1;
                return true;
            }
            return false;
        }

        for (const core::ParamId id : kRequestButtons) {
            if (paramRects_[idx(id)].contains(x, y)) {
                bump(id);
                return true;
            }
        }
        if (stepMinus_.contains(x, y) || stepPlus_.contains(x, y)) {
            const float delta = stepMinus_.contains(x, y) ? -1.0f : 1.0f;
            commit(core::ParamId::captureBars,
                   std::clamp(value(core::ParamId::captureBars) + delta,
                              static_cast<float>(core::kMinCaptureBars),
                              static_cast<float>(core::kMaxCaptureBars)));
            return true;
        }
        for (std::size_t i = 0; i < audioOutRects_.size(); ++i) {
            if (audioOutRects_[i].contains(x, y)) {
                commit(core::ParamId::audioOut, static_cast<float>(i));
                return true;
            }
        }
        if (midiOutRect_.contains(x, y)) {
            commit(core::ParamId::midiOut, value(core::ParamId::midiOut) >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (bbtLoopRect_.contains(x, y)) {
            commit(core::ParamId::bbtLoop, value(core::ParamId::bbtLoop) >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        for (std::size_t i = 0; i < gridRects_.size(); ++i) {
            if (gridRects_[i].contains(x, y)) {
                commit(core::ParamId::grid, static_cast<float>(i));
                return true;
            }
        }
        if (pagePrev_.contains(x, y) || pageNext_.contains(x, y)) {
            const float maxPage = static_cast<float>(std::max(0, (static_cast<int>(core::kMaxEvents)
                                                                   - static_cast<int>(core::kVisibleEvents) - 1)
                                                                  / static_cast<int>(core::kVisibleEvents)));
            const float step = pagePrev_.contains(x, y) ? -1.0f : 1.0f;
            commit(core::ParamId::eventPage,
                   std::clamp(value(core::ParamId::eventPage) + step, 0.0f, maxPage));
            return true;
        }
        if (classPrev_.contains(x, y) || classNext_.contains(x, y)) {
            const float step = classPrev_.contains(x, y) ? -1.0f : 1.0f;
            cycleClass(static_cast<int>(step));
            return true;
        }
        if (paramRects_[idx(core::ParamId::editGhost)].contains(x, y)) {
            commitEdit(core::ParamId::editGhost, value(core::ParamId::editGhost) >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        for (const core::ParamId id : kSliderParams) {
            if (paramRects_[idx(id)].contains(x, y)) {
                const auto& spec = core::kParameterSpecs[idx(id)];
                const Rect r = paramRects_[idx(id)];
                const float fraction = std::clamp((x - r.x) / std::max(1.0f, r.w), 0.0f, 1.0f);
                const float next = spec.minimum + fraction * (spec.maximum - spec.minimum);
                if (id == core::ParamId::editTime || id == core::ParamId::editVelocity)
                    commitEdit(id, next);
                else
                    commit(id, next);
                return true;
            }
        }
        for (std::size_t slot = 0; slot < markerRects_.size(); ++slot) {
            if (markerRects_[slot].contains(x, y)) {
                selectRow(static_cast<int>(slot));
                draggingEvent_ = static_cast<int>(slot);
                return true;
            }
        }
        for (std::size_t row = 0; row < rowRects_.size(); ++row) {
            if (!rowRects_[row].contains(x, y))
                continue;
            const int selected = selectedIndex();
            const int eventIndex = static_cast<int>(value(core::ParamId::eventPage)) * 12 + static_cast<int>(row);
            selectRow(static_cast<int>(row));
            if (selected == eventIndex && x > rowRects_[row].x + 520.0f)
                commitEdit(core::ParamId::editGhost, value(core::ParamId::editGhost) >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (draggingEvent_ < 0 || waveRect_.w <= 0.0f)
            return false;
        const float x = static_cast<float>(ev.pos.getX());
        const float seconds = std::clamp((x - waveRect_.x) / waveRect_.w * spanSeconds(),
                                         0.0f, spanSeconds());
        values_[idx(core::ParamId::editTime)] = seconds;
        repaint();
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!ev.press)
            return false;
        const uint32_t key = ev.key;
        if (key == '[' || key == ']') {
            const float maxPage = static_cast<float>(std::max(0, (static_cast<int>(core::kMaxEvents)
                                                                   - static_cast<int>(core::kVisibleEvents) - 1)
                                                                  / static_cast<int>(core::kVisibleEvents)));
            const float next = std::clamp(value(core::ParamId::eventPage) + (key == '[' ? -1.0f : 1.0f),
                                          0.0f, maxPage);
            commit(core::ParamId::eventPage, next);
            return true;
        }
        if (selectedIndex() < 0)
            return false;
        if (key == 'a' || key == 'd') {
            cycleClass(key == 'a' ? -1 : 1);
            return true;
        }
        if (key == 'w' || key == 's') {
            const float delta = key == 'w' ? 0.05f : -0.05f;
            commitEdit(core::ParamId::editVelocity,
                       std::clamp(value(core::ParamId::editVelocity) + delta, 0.0f, 1.0f));
            return true;
        }
        if (key == 'g') {
            commitEdit(core::ParamId::editGhost, value(core::ParamId::editGhost) >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        if (x < kRightX || y < 440.0f || y > kPanelBottom)
            return false;
        const float maxPage = static_cast<float>(std::max(0, (static_cast<int>(core::kMaxEvents)
                                                               - static_cast<int>(core::kVisibleEvents) - 1)
                                                              / static_cast<int>(core::kVisibleEvents)));
        const float step = ev.delta.getY() > 0.0f ? -1.0f : 1.0f;
        commit(core::ParamId::eventPage,
               std::clamp(value(core::ParamId::eventPage) + step, 0.0f, maxPage));
        return true;
    }

private:
    const laf::Theme& t_ { laf::defaultTheme() };

    static constexpr std::array<core::ParamId, 8> kRequestButtons = {{
        core::ParamId::demoRequest, core::ParamId::captureRequest,
        core::ParamId::analyseRequest, core::ParamId::renderRequest,
        core::ParamId::abSwap, core::ParamId::exportMidiRequest,
        core::ParamId::undoRequest, core::ParamId::redoRequest,
    }};

    static constexpr std::array<core::ParamId, 13> kSliderParams = {{
        core::ParamId::quantiseStrength, core::ParamId::ghostStrength,
        core::ParamId::swing, core::ParamId::humanise, core::ParamId::driftAllowance,
        core::ParamId::transientLevel, core::ParamId::bodyLevel,
        core::ParamId::residualLevel, core::ParamId::saturation,
        core::ParamId::variation, core::ParamId::outputTrim,
        core::ParamId::editTime, core::ParamId::editVelocity,
    }};

    std::array<float, core::kParameterCount> values_ {};
    std::array<Rect, core::kParameterCount> paramRects_ {};
    std::array<Rect, 4> gridRects_ {};
    std::array<Rect, core::kAudioOutNames.size()> audioOutRects_ {};
    Rect midiOutRect_ {};
    Rect bbtLoopRect_ {};
    std::array<Rect, core::kVisibleEvents> markerRects_ {};
    std::array<Rect, core::kVisibleEvents> rowRects_ {};
    Rect stepMinus_ {};
    Rect stepPlus_ {};
    Rect pagePrev_ {};
    Rect pageNext_ {};
    Rect classPrev_ {};
    Rect classNext_ {};
    Rect waveRect_ {};
    int draggingEvent_ = -1;

    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    [[nodiscard]] float value(const core::ParamId id) const noexcept { return values_[idx(id)]; }

    [[nodiscard]] int selectedIndex() const noexcept
    {
        const int selected = static_cast<int>(std::lround(value(core::ParamId::selectedEvent)));
        const int count = static_cast<int>(std::lround(value(core::ParamId::eventCount)));
        return selected >= 0 && selected < count ? selected : -1;
    }

    [[nodiscard]] float spanSeconds() const noexcept
    {
        const float bpm = std::max(1.0f, value(core::ParamId::tempoBpm));
        const float bars = std::clamp(value(core::ParamId::captureBars),
                                      static_cast<float>(core::kMinCaptureBars),
                                      static_cast<float>(core::kMaxCaptureBars));
        float span = std::min(static_cast<float>(core::kMaxCaptureSeconds),
                              bars * 4.0f * 60.0f / bpm);
        for (std::size_t slot = 0; slot < core::kVisibleEvents; ++slot) {
            const std::size_t base = idx(core::ParamId::eventStart) + slot * core::kEventFields;
            span = std::max(span, values_[base] + 0.25f);
            span = std::max(span, values_[base + 4] + 0.25f);
        }
        return span;
    }

    void commit(const core::ParamId id, const float raw)
    {
        const auto& spec = core::kParameterSpecs[idx(id)];
        float next = std::clamp(raw, spec.minimum, spec.maximum);
        if (spec.integer)
            next = std::round(next);
        values_[idx(id)] = next;
        editParameter(static_cast<uint32_t>(id), true);
        setParameterValue(static_cast<uint32_t>(id), next);
        editParameter(static_cast<uint32_t>(id), false);
        repaint();
    }

    void bump(const core::ParamId id)
    {
        const auto& spec = core::kParameterSpecs[idx(id)];
        float next = value(id) + 1.0f;
        if (next > spec.maximum)
            next = spec.minimum;
        commit(id, next);
    }

    void commitEdit(const core::ParamId id, const float next)
    {
        commit(id, next);
        bump(core::ParamId::eventEditRequest);
    }

    void cycleClass(const int direction)
    {
        const int current = static_cast<int>(std::lround(value(core::ParamId::editClass)));
        const int next = std::clamp(current + direction, 0,
                                    static_cast<int>(core::kClassCount) - 1);
        commitEdit(core::ParamId::editClass, static_cast<float>(next));
    }

    void selectRow(const int row)
    {
        const int eventIndex = static_cast<int>(std::lround(value(core::ParamId::eventPage)))
                             * static_cast<int>(core::kVisibleEvents) + row;
        if (eventIndex < 0 || static_cast<std::size_t>(eventIndex) >= core::kMaxEvents)
            return;
        commit(core::ParamId::selectedEvent, static_cast<float>(eventIndex));
        const std::size_t base = idx(core::ParamId::eventStart)
                               + static_cast<std::size_t>(row) * core::kEventFields;
        values_[idx(core::ParamId::editClass)] = values_[base + 1];
        values_[idx(core::ParamId::editTime)] = values_[base + 4];
        values_[idx(core::ParamId::editVelocity)] = values_[base + 2];
        values_[idx(core::ParamId::editGhost)] = values_[base + 5];
        repaint();
    }

    // -----------------------------------------------------------------------

    void drawBackground(const float w, const float h)
    {
        beginPath();
        fc(t_.background);
        rect(0.0f, 0.0f, w, h);
        fill();
        closePath();
    }

    void drawHeader(const float w)
    {
        beginPath();
        fc(t_.panel);
        rect(0.0f, 0.0f, w, 58.0f);
        fill();
        closePath();

        beginPath();
        sc(t_.accent);
        strokeWidth(2.0f);
        moveTo(0.0f, 57.0f);
        lineTo(w, 57.0f);
        stroke();
        closePath();

        fontSize(22.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t_.textPrimary);
        text(20.0f, 28.0f, "ENERGISE", nullptr);

        fontSize(12.0f);
        fc(t_.textDim);
        text(148.0f, 29.0f, "drum break  >  events  >  grid  >  resynthesis", nullptr);

        const int state = std::clamp(static_cast<int>(std::lround(value(core::ParamId::status))),
                                     0, static_cast<int>(core::kStateNames.size()) - 1);
        const bool bbtLocked = value(core::ParamId::bbtValid) >= 0.5f
                               && value(core::ParamId::bbtLoop) >= 0.5f;
        char status[64];
        std::snprintf(status, sizeof(status), "%s  %.0f BPM  %d ev  %s",
                      core::kStateNames[static_cast<std::size_t>(state)],
                      value(core::ParamId::tempoBpm),
                      static_cast<int>(std::lround(value(core::ParamId::eventCount))),
                      bbtLocked ? "BBT" : "FREE");
        beginPath();
        fc(t_.surface);
        roundedRect(w - 290.0f, 13.0f, 270.0f, 32.0f, 5.0f);
        fill();
        sc(t_.border);
        strokeWidth(1.0f);
        stroke();
        closePath();
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fc(t_.accent);
        text(w - 155.0f, 29.0f, status, nullptr);
    }

    void drawButton(const Rect r, const char* label, const bool active)
    {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 4.0f);
        fc(active ? t_.buttonHot : t_.buttonFace);
        fill();
        sc(active ? t_.accent : t_.border);
        strokeWidth(active ? 1.8f : 1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        // Active faces are amber; accent-on-amber made the label disappear.
        // The unlit-lamp housing colour is dark in both themes.
        fc(active ? t_.accentDim : t_.textPrimary);
        text(r.x + r.w * 0.5f, r.y + r.h * 0.5f, label, nullptr);
    }

    void drawTransport(const float w)
    {
        const float y = 70.0f;
        const float h = 46.0f;
        const float gap = 8.0f;
        const float buttonW = (w - 2.0f * kPad - gap * 7.0f) / 8.0f;
        const char* labels[8] = {"Demo", "Capture", "Analyse", "Render", "A/B", "Export MIDI", "Undo", "Redo"};

        for (std::size_t i = 0; i < kRequestButtons.size(); ++i) {
            const Rect r {kPad + static_cast<float>(i) * (buttonW + gap), y, buttonW, h};
            paramRects_[idx(kRequestButtons[i])] = r;
            bool active = false;
            switch (kRequestButtons[i]) {
            case core::ParamId::renderRequest:
                active = value(core::ParamId::renderReady) >= 0.5f;
                break;
            case core::ParamId::abSwap:
                active = value(core::ParamId::abSide) >= 0.5f;
                break;
            default:
                break;
            }
            drawButton(r, labels[i], active);
        }
    }

    void drawPanel(const float x, const float y, const float w, const float h, const char* title)
    {
        beginPath();
        fc(t_.panel);
        roundedRect(x, y, w, h, 5.0f);
        fill();
        sc(t_.bezel);
        strokeWidth(1.0f);
        stroke();
        closePath();

        beginPath();
        fc(t_.surface);
        rect(x + 1.0f, y + 1.0f, w - 2.0f, 26.0f);
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t_.textPrimary);
        text(x + 12.0f, y + 14.0f, title, nullptr);
    }

    void drawSlider(const core::ParamId id, const float x, const float y, const float w,
                    const char* label, const char* unitOverride)
    {
        const auto& spec = core::kParameterSpecs[idx(id)];
        const Rect r {x, y, w, 16.0f};
        paramRects_[idx(id)] = r;
        const float v = value(id);
        const float fraction = std::clamp((v - spec.minimum) / std::max(0.0001f, spec.maximum - spec.minimum),
                                          0.0f, 1.0f);

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t_.textDim);
        text(r.x, r.y + r.h * 0.5f, label, nullptr);

        beginPath();
        fc(t_.controlTrack);
        roundedRect(r.x + 88.0f, r.y + 6.0f, r.w - 88.0f - 62.0f, 5.0f, 2.0f);
        fill();
        closePath();

        beginPath();
        fc(t_.controlFill);
        roundedRect(r.x + 88.0f, r.y + 6.0f, (r.w - 88.0f - 62.0f) * fraction, 5.0f, 2.0f);
        fill();
        closePath();

        beginPath();
        fc(t_.controlKnob);
        circle(r.x + 88.0f + (r.w - 88.0f - 62.0f) * fraction, r.y + 8.5f, 5.0f);
        fill();
        closePath();

        char textBuf[32];
        const char* unit = unitOverride != nullptr ? unitOverride : spec.unit;
        if (std::strcmp(unit, "%") == 0)
            std::snprintf(textBuf, sizeof(textBuf), "%.0f%%", v * 100.0f);
        else if (spec.integer)
            std::snprintf(textBuf, sizeof(textBuf), "%d", static_cast<int>(std::lround(v)));
        else
            std::snprintf(textBuf, sizeof(textBuf), "%.2f", v);
        fontSize(11.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fc(t_.textPrimary);
        text(r.x + r.w, r.y + r.h * 0.5f, textBuf, nullptr);
    }

    void drawCaptureAndGroove()
    {
        const float x = kPad;
        const float y = kPanelTop;
        drawPanel(x, y, kLeftW, 232.0f, "CAPTURE / GROOVE  —  NEXT BAR");

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t_.textDim);
        text(x + 18.0f, y + 48.0f, "BARS", nullptr);

        stepMinus_ = {x + 70.0f, y + 38.0f, 24.0f, 22.0f};
        stepPlus_ = {x + 174.0f, y + 38.0f, 24.0f, 22.0f};
        drawButton(stepMinus_, "-", false);
        drawButton(stepPlus_, "+", false);
        char length[40];
        const float bpm = std::max(1.0f, value(core::ParamId::tempoBpm));
        const float seconds = std::min(static_cast<float>(core::kMaxCaptureSeconds),
                                       value(core::ParamId::captureBars) * 4.0f * 60.0f / bpm);
        std::snprintf(length, sizeof(length), "%d bars  %.1f s",
                      static_cast<int>(std::lround(value(core::ParamId::captureBars))), seconds);
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fc(t_.textPrimary);
        text(x + 134.0f, y + 49.0f, length, nullptr);

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(value(core::ParamId::bbtValid) >= 0.5f ? t_.accent : t_.textDim);
        text(x + 210.0f, y + 49.0f,
             value(core::ParamId::bbtValid) >= 0.5f ? "BBT OK" : "NO BBT", nullptr);

        fontSize(11.0f);
        fc(t_.textDim);
        text(x + 18.0f, y + 86.0f, "GRID", nullptr);
        const float gridY = y + 76.0f;
        const float gridW = (kLeftW - 92.0f - 18.0f) / 4.0f;
        for (std::size_t i = 0; i < gridRects_.size(); ++i) {
            gridRects_[i] = {x + 72.0f + static_cast<float>(i) * (gridW + 4.0f), gridY, gridW, 24.0f};
            drawButton(gridRects_[i], core::kGridNames[i],
                       static_cast<int>(std::lround(value(core::ParamId::grid))) == static_cast<int>(i));
        }

        drawSlider(core::ParamId::quantiseStrength, x + 18.0f, y + 120.0f, kLeftW - 36.0f, "Quantise", nullptr);
        drawSlider(core::ParamId::ghostStrength, x + 18.0f, y + 148.0f, kLeftW - 36.0f, "Ghosts", nullptr);
        drawSlider(core::ParamId::swing, x + 18.0f, y + 176.0f, kLeftW - 36.0f, "Swing", nullptr);
        drawSlider(core::ParamId::humanise, x + 18.0f, y + 204.0f, kLeftW - 36.0f, "Humanise", nullptr);
    }

    void drawOutputSync()
    {
        const float x = kPad;
        const float y = 376.0f;
        drawPanel(x, y, kLeftW, 104.0f, "OUTPUT / SYNC");

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t_.textDim);
        text(x + 16.0f, y + 43.0f, "AUDIO", nullptr);
        const float segW = (kLeftW - 78.0f - 16.0f - 12.0f) / core::kAudioOutNames.size();
        const int mode = static_cast<int>(std::lround(value(core::ParamId::audioOut)));
        for (std::size_t i = 0; i < audioOutRects_.size(); ++i) {
            audioOutRects_[i] = {x + 78.0f + static_cast<float>(i) * (segW + 6.0f),
                                 y + 32.0f, segW, 22.0f};
            drawButton(audioOutRects_[i], core::kAudioOutNames[i], static_cast<int>(i) == mode);
        }

        fontSize(10.0f);
        fc(t_.textDim);
        text(x + 16.0f, y + 75.0f, "MIDI OUT", nullptr);
        midiOutRect_ = {x + 104.0f, y + 64.0f, 44.0f, 22.0f};
        const bool midiOn = core::midiOutEnabled(value(core::ParamId::midiOut));
        drawButton(midiOutRect_, midiOn ? "ON" : "OFF", midiOn);

        fc(t_.textDim);
        text(x + 172.0f, y + 75.0f, "BBT LOOP", nullptr);
        bbtLoopRect_ = {x + 260.0f, y + 64.0f, 44.0f, 22.0f};
        const bool loopOn = core::bbtLoopEnabled(value(core::ParamId::bbtLoop));
        drawButton(bbtLoopRect_, loopOn ? "ON" : "OFF", loopOn);
    }

    void drawSound()
    {
        const float x = kPad;
        const float y = 492.0f;
        drawPanel(x, y, kLeftW, 190.0f, "REBUILT VOICES");

        drawSlider(core::ParamId::transientLevel, x + 18.0f, y + 34.0f, kLeftW - 36.0f, "Transient", nullptr);
        drawSlider(core::ParamId::bodyLevel, x + 18.0f, y + 62.0f, kLeftW - 36.0f, "Body", nullptr);
        drawSlider(core::ParamId::residualLevel, x + 18.0f, y + 90.0f, kLeftW - 36.0f, "Residual", nullptr);
        drawSlider(core::ParamId::saturation, x + 18.0f, y + 118.0f, kLeftW - 36.0f, "Saturation", nullptr);
        drawSlider(core::ParamId::variation, x + 18.0f, y + 146.0f, kLeftW - 36.0f, "Variation", nullptr);
        drawSlider(core::ParamId::outputTrim, x + 18.0f, y + 174.0f, kLeftW - 36.0f, "Output", nullptr);
    }

    void drawHelp()
    {
        const float x = kPad;
        const float y = 694.0f;
        drawPanel(x, y, kLeftW, 74.0f, "WORKFLOW");

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t_.textDim);
        text(x + 14.0f, y + 44.0f, "1 Demo/Capture at bar  2 Analyse  3 Edit  4 Render", nullptr);
        text(x + 14.0f, y + 62.0f, "Audio Off/Pass/Render  MIDI  BBT loop  A/D W/S G", nullptr);
    }

    void drawWaveform()
    {
        const float x = kRightX;
        const float y = kPanelTop;
        drawPanel(x, y, kRightW, 220.0f, "WAVEFORM / EVENT LANE");

        waveRect_ = {x + 14.0f, y + 40.0f, kRightW - 28.0f, 162.0f};
        beginPath();
        fc(t_.background);
        rect(waveRect_.x, waveRect_.y, waveRect_.w, waveRect_.h);
        fill();
        sc(t_.border);
        strokeWidth(1.0f);
        stroke();
        closePath();

        for (int i = 1; i < 8; ++i) {
            const float gx = waveRect_.x + waveRect_.w * static_cast<float>(i) / 8.0f;
            beginPath();
            sc(t_.meterOff);
            strokeWidth(1.0f);
            moveTo(gx, waveRect_.y);
            lineTo(gx, waveRect_.y + waveRect_.h);
            stroke();
            closePath();
        }

        const float mid = waveRect_.y + waveRect_.h * 0.5f;
        beginPath();
        sc(t_.border);
        strokeWidth(1.0f);
        moveTo(waveRect_.x, mid);
        lineTo(waveRect_.x + waveRect_.w, mid);
        stroke();
        closePath();

        const std::size_t base = idx(core::ParamId::waveStart);
        const float barW = waveRect_.w / static_cast<float>(core::kWaveformPoints);
        for (std::size_t i = 0; i < core::kWaveformPoints; ++i) {
            const float v = std::clamp(values_[base + i], -1.0f, 1.0f);
            const float bx = waveRect_.x + static_cast<float>(i) * barW + 1.0f;
            const float top = mid - std::fabs(v) * waveRect_.h * 0.46f;
            const float height = std::max(2.0f, mid - top);
            beginPath();
            fc(v < 0.0f ? t_.accentDim : t_.controlFill);
            rect(bx, v < 0.0f ? mid : top, std::max(1.0f, barW - 2.0f), height);
            fill();
            closePath();
        }

        const float span = spanSeconds();
        const int selected = selectedIndex();
        const int page = static_cast<int>(std::lround(value(core::ParamId::eventPage)));
        for (std::size_t slot = 0; slot < core::kVisibleEvents; ++slot) {
            const std::size_t offset = idx(core::ParamId::eventStart) + slot * core::kEventFields;
            const int eventIndex = page * static_cast<int>(core::kVisibleEvents) + static_cast<int>(slot);
            if (eventIndex >= static_cast<int>(std::lround(value(core::ParamId::eventCount)))) {
                markerRects_[slot] = {};
                continue;
            }
            const float source = std::clamp(values_[offset] / span, 0.0f, 1.0f);
            const float quant = std::clamp(values_[offset + 4] / span, 0.0f, 1.0f);
            const float sx = waveRect_.x + source * waveRect_.w;
            const float qx = waveRect_.x + quant * waveRect_.w;
            const bool isSelected = eventIndex == selected;
            const bool ghost = values_[offset + 5] >= 0.5f;

            if (std::fabs(qx - sx) > 2.0f) {
                const float arrowY = waveRect_.y + waveRect_.h - 12.0f;
                beginPath();
                sc(isSelected ? t_.accent : t_.accentDim);
                strokeWidth(1.0f);
                moveTo(sx, arrowY);
                lineTo(qx, arrowY);
                lineTo(qx - 4.0f, arrowY - 3.0f);
                moveTo(qx, arrowY);
                lineTo(qx - 4.0f, arrowY + 3.0f);
                stroke();
                closePath();
            }

            beginPath();
            sc(isSelected ? t_.warning : t_.accent);
            strokeWidth(isSelected ? 2.4f : 1.2f);
            moveTo(qx, waveRect_.y + 6.0f);
            lineTo(qx, waveRect_.y + waveRect_.h - 20.0f);
            stroke();
            closePath();

            beginPath();
            fc(isSelected ? t_.warning : t_.accentDim);
            circle(sx, waveRect_.y + 8.0f, ghost ? 2.2f : 3.4f);
            fill();
            closePath();

            markerRects_[slot] = {qx - 6.0f, waveRect_.y, 12.0f, waveRect_.h - 18.0f};
        }
    }

    void drawEditStrip()
    {
        const float x = kRightX;
        const float y = 362.0f;
        drawPanel(x, y, kRightW, 68.0f, "EDIT SELECTED EVENT");

        const int selected = selectedIndex();
        char heading[64];
        if (selected < 0) {
            std::snprintf(heading, sizeof(heading), "click a marker or table row");
        } else {
            const int cls = static_cast<int>(std::lround(value(core::ParamId::editClass)));
            std::snprintf(heading, sizeof(heading), "#%d  %s", selected + 1,
                          core::kClassNames[std::clamp(cls, 0, static_cast<int>(core::kClassCount) - 1)]);
        }
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t_.textPrimary);
        text(x + 14.0f, y + 46.0f, heading, nullptr);

        classPrev_ = {x + 218.0f, y + 34.0f, 22.0f, 22.0f};
        classNext_ = {x + 244.0f, y + 34.0f, 22.0f, 22.0f};
        drawButton(classPrev_, "<", false);
        drawButton(classNext_, ">", false);

        fontSize(10.0f);
        fc(t_.textDim);
        text(x + 290.0f, y + 43.0f, "TIME", nullptr);
        drawSlider(core::ParamId::editTime, x + 290.0f, y + 34.0f, 210.0f, "", "s");

        fontSize(10.0f);
        fc(t_.textDim);
        text(x + 522.0f, y + 43.0f, "VEL", nullptr);
        drawSlider(core::ParamId::editVelocity, x + 522.0f, y + 34.0f, 150.0f, "", nullptr);

        const Rect ghost {x + 686.0f, y + 34.0f, 44.0f, 22.0f};
        paramRects_[idx(core::ParamId::editGhost)] = ghost;
        drawButton(ghost, "GHOST", value(core::ParamId::editGhost) >= 0.5f);
    }

    void drawEventTable()
    {
        const float x = kRightX;
        const float y = 440.0f;
        drawPanel(x, y, kRightW, 260.0f, "EVENT LIST  (source > quantised)");

        pagePrev_ = {x + kRightW - 76.0f, y + 4.0f, 24.0f, 18.0f};
        pageNext_ = {x + kRightW - 46.0f, y + 4.0f, 24.0f, 18.0f};
        drawButton(pagePrev_, "<", false);
        drawButton(pageNext_, ">", false);

        const float rowY = y + 32.0f;
        const float rowH = 17.0f;
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t_.textDim);
        text(x + 16.0f, rowY - 8.0f, "#", nullptr);
        text(x + 46.0f, rowY - 8.0f, "TIME", nullptr);
        text(x + 122.0f, rowY - 8.0f, "CLASS", nullptr);
        text(x + 274.0f, rowY - 8.0f, "VEL", nullptr);
        text(x + 362.0f, rowY - 8.0f, "CONF", nullptr);
        text(x + 450.0f, rowY - 8.0f, "QUANT", nullptr);
        text(x + 552.0f, rowY - 8.0f, "GHOST", nullptr);

        const int page = static_cast<int>(std::lround(value(core::ParamId::eventPage)));
        const int count = static_cast<int>(std::lround(value(core::ParamId::eventCount)));
        const int selected = selectedIndex();

        for (std::size_t slot = 0; slot < core::kVisibleEvents; ++slot) {
            const float ry = rowY + static_cast<float>(slot) * rowH;
            rowRects_[slot] = {x + 8.0f, ry, kRightW - 16.0f, rowH};
            const int eventIndex = page * static_cast<int>(core::kVisibleEvents) + static_cast<int>(slot);
            if (eventIndex >= count) {
                markerRects_[slot] = {};
                continue;
            }

            if (eventIndex == selected) {
                beginPath();
                fc(t_.selection);
                rect(rowRects_[slot].x, ry, rowRects_[slot].w, rowH);
                fill();
                closePath();
            }

            const std::size_t base = idx(core::ParamId::eventStart) + slot * core::kEventFields;
            const int cls = std::clamp(static_cast<int>(std::lround(values_[base + 1])), 0,
                                       static_cast<int>(core::kClassCount) - 1);
            char cell[48];
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fc(eventIndex == selected ? t_.textPrimary : t_.textDim);
            const float cy = ry + rowH * 0.5f;
            std::snprintf(cell, sizeof(cell), "%d", eventIndex + 1);
            text(x + 16.0f, cy, cell, nullptr);
            std::snprintf(cell, sizeof(cell), "%.3f", values_[base]);
            text(x + 46.0f, cy, cell, nullptr);
            text(x + 122.0f, cy, core::kClassNames[static_cast<std::size_t>(cls)], nullptr);
            std::snprintf(cell, sizeof(cell), "%.0f%%", values_[base + 2] * 100.0f);
            text(x + 274.0f, cy, cell, nullptr);
            std::snprintf(cell, sizeof(cell), "%.0f%%", values_[base + 3] * 100.0f);
            text(x + 362.0f, cy, cell, nullptr);
            std::snprintf(cell, sizeof(cell), "%.3f", values_[base + 4]);
            text(x + 450.0f, cy, cell, nullptr);
            text(x + 552.0f, cy, values_[base + 5] >= 0.5f ? "yes" : "-", nullptr);
        }
    }
};

UI* createUI()
{
    return new EnergiseUI();
}

END_NAMESPACE_DISTRHO
