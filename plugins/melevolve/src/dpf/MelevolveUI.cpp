#include "DistrhoUI.hpp"

#include "melevolve_core.hpp"
#include "melevolve_messages.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::melevolve;
namespace messages = downspout::melevolve::messages;

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

struct Color {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;
};

// Melogen's light palette (styles/main.css), kept as plain NanoVG colours.
constexpr Color kBg {246, 245, 241};
constexpr Color kSurface {255, 255, 255};
constexpr Color kSurface2 {236, 235, 229};
constexpr Color kText {28, 29, 32};
constexpr Color kMuted {98, 101, 108};
constexpr Color kLine {214, 212, 203};
constexpr Color kAccent {43, 89, 195};
constexpr Color kPlay {23, 128, 61};
constexpr Color kDown {179, 38, 30};
constexpr Color kWhite {255, 255, 255};

constexpr int kTabCount = 4;
constexpr int kTabBase = 0;
constexpr int kPlayBase = 10;
constexpr int kVoteBase = 20;
constexpr int kMore = 40;
constexpr int kEvolve = 41;
constexpr int kNewPopulation = 42;
constexpr int kReset = 50;
constexpr int kParamBase = 100;
constexpr int kStepperBase = 200;
constexpr int kSegmentBase = 300;

const char* originLabel(const char* origin)
{
    if (origin == nullptr) return "";
    const std::string text(origin);
    if (text == "immigrant") return "fresh";
    if (text == "crossover") return "bred";
    if (text == "mutation") return "mutated";
    if (text == "elite") return "kept";
    return "new";
}

std::string noteName(const int midi)
{
    if (midi < 0 || midi > 127)
        return "-";
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%s%d", core::rootName(midi % 12), midi / 12 - 1);
    return buffer;
}

std::string withNoteName(const int midi, const int velocity)
{
    if (midi < 0)
        return "waiting for a note";
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%s  (velocity %d)", noteName(midi).c_str(), velocity);
    return buffer;
}

}  // namespace

class MelevolveUI : public UI {
public:
    MelevolveUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            values_[static_cast<std::size_t>(i)] = core::kParameterSpecs[static_cast<std::size_t>(i)].defaultValue;
        resetRuntimeMirror();
    #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    #else
        loadSharedResources();
    #endif
    }

protected:
    void parameterChanged(const std::uint32_t index, const float value) override
    {
        if (index >= core::kParameterCount)
            return;
        values_[static_cast<std::size_t>(index)] = value;
        if (index <= core::kSendClock)
            mirrorSettingsFromValues(false);
        repaint();
    }

    void stateChanged(const char* const key, const char* const text) override
    {
        if (key == nullptr || text == nullptr || std::strcmp(key, messages::kSession) != 0)
            return;
        if (const auto session = core::deserializeSession(text)) {
            session_ = *session;
            resetRuntimeMirror();
            syncValuesFromMirror(false);
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        actionCount_ = 0;
        sliderCount_ = 0;
        dropdownCount_ = 0;

        fillRect({0.0f, 0.0f, static_cast<float>(getWidth()), static_cast<float>(getHeight())}, kBg);
        drawMasthead();
        drawTabs();

        const Rect content {24.0f, 130.0f, 872.0f, 546.0f};
        switch (activeTab_) {
        case 0: drawInteract(content); break;
        case 1: drawSettings(content); break;
        case 2: drawOutput(content); break;
        default: drawAbout(content); break;
        }
        drawOpenDropdown();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        if (!ev.press) {
            activeSlider_ = -1;
            return false;
        }

        if (openDropdown_ >= 0) {
            const int chosen = dropdownChoiceAt(mx, my);
            openDropdown_ = -1;
            if (chosen >= 0)
                chooseDropdown(chosen);
            repaint();
            return true;
        }

        for (std::size_t i = 0; i < dropdownCount_; ++i) {
            if (dropdowns_[static_cast<std::size_t>(i)].box.contains(mx, my)) {
                openDropdown_ = static_cast<int>(i);
                repaint();
                return true;
            }
        }

        for (std::size_t i = 0; i < actionCount_; ++i) {
            const ActionRect& action = actions_[static_cast<std::size_t>(i)];
            if (action.bounds.contains(mx, my)) {
                onAction(action.id);
                return true;
            }
        }

        for (std::size_t i = 0; i < sliderCount_; ++i) {
            if (sliders_[static_cast<std::size_t>(i)].bounds.contains(mx, my)) {
                activeSlider_ = static_cast<int>(i);
                updateSliderFromMouse(mx);
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ < 0 || activeSlider_ >= static_cast<int>(sliderCount_))
            return false;
        updateSliderFromMouse(static_cast<float>(ev.pos.getX()));
        return true;
    }

private:
    struct ActionRect {
        int id = -1;
        Rect bounds {};
    };
    struct SliderRect {
        core::Param parameter = core::kMode;
        Rect bounds {};
    };
    struct Dropdown {
        core::Param parameter = core::kMode;
        Rect box {};
        int count = 0;
    };

    core::Params values_ {};
    core::Session session_ {};
    core::Engine mirrorEngine_ {};

    int activeTab_ = 0;
    std::array<ActionRect, 128> actions_ {};
    std::size_t actionCount_ = 0;
    std::array<SliderRect, 8> sliders_ {};
    std::size_t sliderCount_ = 0;
    int activeSlider_ = -1;
    std::array<Dropdown, 16> dropdowns_ {};
    std::size_t dropdownCount_ = 0;
    int openDropdown_ = -1;

    // ---- NanoVG primitives -----------------------------------------------------------------------------------

    void fillRect(const Rect r, const Color c, const float radius = 0.0f)
    {
        beginPath();
        fillColor(c.r, c.g, c.b, c.a);
        if (radius > 0.0f)
            roundedRect(r.x, r.y, r.w, r.h, radius);
        else
            rect(r.x, r.y, r.w, r.h);
        fill();
    }

    void strokeRect(const Rect r, const Color c, const float width = 1.0f, const float radius = 0.0f)
    {
        beginPath();
        strokeColor(c.r, c.g, c.b, c.a);
        strokeWidth(width);
        if (radius > 0.0f)
            roundedRect(r.x, r.y, r.w, r.h, radius);
        else
            rect(r.x, r.y, r.w, r.h);
        stroke();
    }

    void labelText(const float x, const float y, const std::string& value, const Color c,
                   const float size = 13.0f, const int align = ALIGN_LEFT | ALIGN_MIDDLE)
    {
        fillColor(c.r, c.g, c.b, c.a);
        fontSize(size);
        textAlign(align);
        text(x, y, value.c_str(), nullptr);
    }

    void rememberAction(const int id, const Rect bounds)
    {
        // The arrays are sized for the full panel; this guard keeps a future control from corrupting them.
        if (actionCount_ < actions_.size())
            actions_[actionCount_++] = {id, bounds};
    }

    // ---- State and actions -------------------------------------------------------------------------------------

    void resetRuntimeMirror()
    {
        mirrorEngine_ = core::Engine {};
        mirrorEngine_.source = session_.settings.clockSource;
        mirrorEngine_.settingsSendClock = session_.settings.sendClock;
    }

    void syncValuesFromMirror(const bool notifyHost)
    {
        const core::Params p = core::paramsFromSettings(session_.settings);
        for (std::uint32_t i = 0; i <= core::kSendClock; ++i) {
            values_[static_cast<std::size_t>(i)] = p[static_cast<std::size_t>(i)];
            if (notifyHost)
                setParameterValue(i, p[static_cast<std::size_t>(i)]);
        }
    }

    void mirrorSettingsFromValues(const bool notifyHost)
    {
        core::applySettings(session_, mirrorEngine_, core::settingsFromParams(values_));
        syncValuesFromMirror(notifyHost);
    }

    void commitParameter(const core::Param parameter, const float raw)
    {
        const auto& spec = core::kParameterSpecs[static_cast<std::size_t>(parameter)];
        float value = std::clamp(raw, spec.minimum, spec.maximum);
        if (spec.integer)
            value = std::round(value);
        values_[static_cast<std::size_t>(parameter)] = value;
        setParameterValue(static_cast<std::uint32_t>(parameter), value);
        mirrorSettingsFromValues(false);
        repaint();
    }

    void sendAction(const std::string& text)
    {
        setState(messages::kAction, text.c_str());
    }

    void onAction(const int id)
    {
        if (id >= kTabBase && id < kTabBase + kTabCount) {
            activeTab_ = id - kTabBase;
            openDropdown_ = -1;
            repaint();
            return;
        }
        if (id >= kPlayBase && id < kPlayBase + core::kRows) {
            const int position = id - kPlayBase;
            sendAction(messages::play(position));
            core::requestPlay(session_, mirrorEngine_, position);
            repaint();
            return;
        }
        if (id >= kVoteBase && id < kVoteBase + core::kRows * 3) {
            const int position = (id - kVoteBase) / 3;
            static constexpr int ratings[3] = {-1, 0, 1};
            sendAction(messages::vote(position, ratings[(id - kVoteBase) % 3]));
            core::applyAction(session_, mirrorEngine_, core::parseAction(messages::vote(position, ratings[(id - kVoteBase) % 3])));
            repaint();
            return;
        }
        if (id == kMore || id == kEvolve || id == kNewPopulation) {
            const std::string text = id == kMore ? messages::more()
                : id == kEvolve ? messages::evolve() : messages::newPopulation();
            sendAction(text);
            core::applyAction(session_, mirrorEngine_, core::parseAction(text));
            repaint();
            return;
        }
        if (id == kParamBase + core::kLoop || id == kParamBase + core::kSendClock) {
            const auto parameter = id == kParamBase + core::kLoop ? core::kLoop : core::kSendClock;
            commitParameter(parameter, values_[static_cast<std::size_t>(parameter)] > 0.5f ? 0.0f : 1.0f);
            return;
        }
        if (id == kSegmentBase) {
            commitParameter(core::kMode, values_[static_cast<std::size_t>(core::kMode)] > 0.5f ? 0.0f : 1.0f);
            return;
        }
        if (id == kSegmentBase + 1) {
            commitParameter(core::kMode, values_[static_cast<std::size_t>(core::kMode)] > 0.5f ? 1.0f : 0.0f);
            return;
        }
        if (id >= kStepperBase) {
            const int encoded = id - kStepperBase;
            const auto parameter = static_cast<core::Param>(encoded / 2);
            const int direction = encoded % 2 == 0 ? -1 : 1;
            if (parameter > core::kSendClock)
                return;
            commitParameter(parameter, values_[static_cast<std::size_t>(parameter)] + static_cast<float>(direction));
            return;
        }
        if (id == kReset) {
            sendAction(messages::resetAll());
            core::applyAction(session_, mirrorEngine_, core::parseAction(messages::resetAll()));
            syncValuesFromMirror(true);
            repaint();
        }
    }

    void updateSliderFromMouse(const float mx)
    {
        if (activeSlider_ < 0 || activeSlider_ >= static_cast<int>(sliderCount_))
            return;
        const SliderRect& slider = sliders_[static_cast<std::size_t>(activeSlider_)];
        const auto& spec = core::kParameterSpecs[static_cast<std::size_t>(slider.parameter)];
        const float normalized = std::clamp((mx - slider.bounds.x) / std::max(1.0f, slider.bounds.w), 0.0f, 1.0f);
        commitParameter(slider.parameter, spec.minimum + normalized * (spec.maximum - spec.minimum));
    }

    // ---- Dropdowns ----------------------------------------------------------------------------------------------

    static int itemCount(const core::Param parameter)
    {
        switch (parameter) {
        case core::kRoot: return 12;
        case core::kScale: return static_cast<int>(core::ScaleId::count);
        case core::kTimeSignature: return core::kTimeSignatureCount;
        case core::kClockSource: return 3;
        case core::kMidiChannel: return 16;
        default: return 0;
        }
    }

    static int itemOffset(const core::Param parameter)
    {
        return parameter == core::kMidiChannel ? 1 : 0;
    }

    static std::string itemText(const core::Param parameter, const int index)
    {
        char buffer[32];
        switch (parameter) {
        case core::kRoot: return core::rootName(index);
        case core::kScale: return core::scaleName(static_cast<core::ScaleId>(index));
        case core::kTimeSignature: return core::timeSignatureName(index);
        case core::kClockSource: return core::clockSourceName(static_cast<core::ClockSource>(index));
        case core::kMidiChannel:
            std::snprintf(buffer, sizeof(buffer), "Channel %d", index + 1);
            return buffer;
        default: return "";
        }
    }

    [[nodiscard]] Rect dropdownPopup(const Dropdown& dropdown) const
    {
        const int count = dropdown.count;
        const int columns = count > 10 ? 2 : 1;
        const int rows = (count + columns - 1) / columns;
        constexpr float itemH = 24.0f;
        const float columnW = dropdown.parameter == core::kScale ? 156.0f : std::max(dropdown.box.w, 128.0f);
        const float width = columnW * static_cast<float>(columns);
        const float height = itemH * static_cast<float>(rows) + 8.0f;
        const float x = std::clamp(dropdown.box.x, 8.0f, static_cast<float>(getWidth()) - width - 8.0f);
        const float below = dropdown.box.y + dropdown.box.h + 3.0f;
        const float y = below + height <= static_cast<float>(getHeight()) - 8.0f
            ? below : std::max(8.0f, dropdown.box.y - 3.0f - height);
        return {x, y, width, height};
    }

    int dropdownChoiceAt(const float mx, const float my) const
    {
        if (openDropdown_ < 0 || openDropdown_ >= static_cast<int>(dropdownCount_))
            return -1;
        const Dropdown& dropdown = dropdowns_[static_cast<std::size_t>(openDropdown_)];
        const Rect popup = dropdownPopup(dropdown);
        if (!popup.contains(mx, my))
            return -1;
        const int columns = dropdown.count > 10 ? 2 : 1;
        const int rows = (dropdown.count + columns - 1) / columns;
        const float columnW = popup.w / static_cast<float>(columns);
        const int column = std::clamp(static_cast<int>((mx - popup.x) / columnW), 0, columns - 1);
        const int row = std::clamp(static_cast<int>((my - popup.y - 4.0f) / 24.0f), 0, rows - 1);
        const int index = column * rows + row;
        return index < dropdown.count ? index : -1;
    }

    void chooseDropdown(const int index)
    {
        if (openDropdown_ < 0 || openDropdown_ >= static_cast<int>(dropdownCount_))
            return;
        const Dropdown dropdown = dropdowns_[static_cast<std::size_t>(openDropdown_)];
        commitParameter(dropdown.parameter, static_cast<float>(index + itemOffset(dropdown.parameter)));
    }

    // ---- Masthead, tabs and shared controls ---------------------------------------------------------------------

    void drawMasthead()
    {
        fillRect({0.0f, 0.0f, static_cast<float>(getWidth()), 116.0f}, kSurface);
        labelText(24.0f, 28.0f, "Melevolve", kText, 27.0f);
        labelText(156.0f, 31.0f, "evolutionary melodies, straight to MIDI", kMuted, 14.0f);
        labelText(24.0f, 56.0f,
                  "Play five melodies, vote - 0 +, then bring on more or breed the next generation.",
                  kMuted, 12.5f);
        labelText(static_cast<float>(getWidth()) - 24.0f, 31.0f, "DOWNSPOUT", kMuted, 11.0f,
                  ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawTabs()
    {
        static constexpr const char* names[kTabCount] = {"Interact", "Settings", "MIDI / Clock", "About"};
        const float gap = 4.0f;
        const float width = (872.0f - gap * static_cast<float>(kTabCount - 1)) / static_cast<float>(kTabCount);
        fillRect({24.0f, 76.0f, 872.0f, 42.0f}, kSurface2, 12.0f);
        for (int i = 0; i < kTabCount; ++i) {
            const Rect tab {24.0f + static_cast<float>(i) * (width + gap), 80.0f, width, 34.0f};
            const bool active = i == activeTab_;
            fillRect(tab, active ? kSurface : kSurface2, 9.0f);
            if (active)
                strokeRect(tab, kLine, 1.0f, 9.0f);
            labelText(tab.x + tab.w * 0.5f, tab.y + tab.h * 0.5f, names[i], active ? kText : kMuted,
                      active ? 14.0f : 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
            rememberAction(kTabBase + i, tab);
        }
    }

    void drawButton(const int id, const std::string& text, const Rect r, const Color face,
                    const Color foreground, const bool enabled = true)
    {
        const Color shown = enabled ? face : kSurface2;
        fillRect(r, shown, 10.0f);
        if (face.r != kSurface.r || face.g != kSurface.g || face.b != kSurface.b)
            strokeRect(r, enabled ? face : kLine, 1.0f, 10.0f);
        else
            strokeRect(r, kLine, 1.0f, 10.0f);
        labelText(r.x + r.w * 0.5f, r.y + r.h * 0.5f, text, enabled ? foreground : kMuted,
                  13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        rememberAction(id, r);
    }

    void drawFieldLabel(const Rect field, const char* text)
    {
        labelText(field.x, field.y + 9.0f, text, kMuted, 11.5f);
    }

    void drawSelect(const core::Param parameter, const char* text, const Rect field)
    {
        drawFieldLabel(field, text);
        const Rect box {field.x, field.y + 18.0f, field.w, 34.0f};
        fillRect(box, kSurface, 9.0f);
        strokeRect(box, kLine, 1.0f, 9.0f);
        const int index = std::clamp(static_cast<int>(std::lround(values_[static_cast<std::size_t>(parameter)]))
                                     - itemOffset(parameter), 0, itemCount(parameter) - 1);
        labelText(box.x + 10.0f, box.y + box.h * 0.5f, itemText(parameter, index), kText, 12.5f);
        labelText(box.x + box.w - 10.0f, box.y + box.h * 0.5f, "v", kMuted, 11.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        dropdowns_[dropdownCount_++] = {parameter, box, itemCount(parameter)};
    }

    void drawStepper(const core::Param parameter, const char* text, const Rect field, const std::string& value)
    {
        drawFieldLabel(field, text);
        const float y = field.y + 18.0f;
        const Rect left {field.x, y, 34.0f, 34.0f};
        const Rect right {field.x + field.w - 34.0f, y, 34.0f, 34.0f};
        fillRect(left, kSurface2, 8.0f);
        fillRect(right, kSurface2, 8.0f);
        strokeRect(left, kLine, 1.0f, 8.0f);
        strokeRect(right, kLine, 1.0f, 8.0f);
        labelText(left.x + left.w * 0.5f, left.y + left.h * 0.5f, "-", kText, 16.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        labelText(right.x + right.w * 0.5f, right.y + right.h * 0.5f, "+", kText, 16.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        labelText(field.x + field.w * 0.5f, y + 17.0f, value, kText, 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        rememberAction(kStepperBase + static_cast<int>(parameter) * 2, left);
        rememberAction(kStepperBase + static_cast<int>(parameter) * 2 + 1, right);
    }

    void drawToggle(const core::Param parameter, const char* text, const Rect field)
    {
        drawFieldLabel(field, text);
        const bool on = values_[static_cast<std::size_t>(parameter)] > 0.5f;
        const Rect track {field.x, field.y + 20.0f, 52.0f, 28.0f};
        fillRect(track, on ? kAccent : kSurface2, 14.0f);
        strokeRect(track, on ? kAccent : kLine, 1.0f, 14.0f);
        const float knobX = on ? track.x + track.w - 13.0f : track.x + 13.0f;
        beginPath();
        fillColor(kWhite.r, kWhite.g, kWhite.b, kWhite.a);
        circle(knobX, track.y + track.h * 0.5f, 10.0f);
        fill();
        labelText(track.x + track.w + 12.0f, track.y + track.h * 0.5f, on ? "On" : "Off", kMuted, 12.0f);
        rememberAction(kParamBase + static_cast<int>(parameter), track);
    }

    void drawSlider(const core::Param parameter, const char* text, const Rect field, const std::string& value)
    {
        drawFieldLabel(field, text);
        const auto& spec = core::kParameterSpecs[static_cast<std::size_t>(parameter)];
        const Rect track {field.x, field.y + 29.0f, field.w - 66.0f, 12.0f};
        fillRect(track, kSurface2, 5.0f);
        const float normalized = std::clamp((values_[static_cast<std::size_t>(parameter)] - spec.minimum)
                                            / (spec.maximum - spec.minimum), 0.0f, 1.0f);
        fillRect({track.x, track.y, track.w * normalized, track.h}, kAccent, 5.0f);
        labelText(field.x + field.w, field.y + 9.0f, value, kMuted, 12.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        labelText(field.x + field.w, field.y + 35.0f, value, kText, 12.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        if (sliderCount_ < sliders_.size())
            sliders_[sliderCount_++] = {parameter, {track.x, track.y - 7.0f, track.w, 26.0f}};
    }

    void drawOpenDropdown()
    {
        if (openDropdown_ < 0 || openDropdown_ >= static_cast<int>(dropdownCount_))
            return;
        const Dropdown dropdown = dropdowns_[static_cast<std::size_t>(openDropdown_)];
        const Rect popup = dropdownPopup(dropdown);
        fillRect(popup, kWhite, 10.0f);
        strokeRect(popup, kLine, 1.0f, 10.0f);
        const int columns = dropdown.count > 10 ? 2 : 1;
        const int rows = (dropdown.count + columns - 1) / columns;
        const float columnW = popup.w / static_cast<float>(columns);
        const int selected = static_cast<int>(std::lround(values_[static_cast<std::size_t>(dropdown.parameter)]))
            - itemOffset(dropdown.parameter);
        for (int i = 0; i < dropdown.count; ++i) {
            const Rect item {popup.x + static_cast<float>(i / rows) * columnW + 4.0f,
                             popup.y + 4.0f + static_cast<float>(i % rows) * 24.0f,
                             columnW - 8.0f, 24.0f};
            if (i == selected)
                fillRect(item, kSurface2, 5.0f);
            labelText(item.x + 8.0f, item.y + item.h * 0.5f, itemText(dropdown.parameter, i),
                      i == selected ? kAccent : kText, 12.0f);
        }
    }

    // ---- Interact ------------------------------------------------------------------------------------------------

    void drawInteract(const Rect content)
    {
        labelText(content.x, content.y + 6.0f,
                  "Play each melody and vote. More shows the next five; Evolve breeds from your votes.",
                  kMuted, 12.0f);
        labelText(content.x, content.y + 34.0f,
                  "Generation " + std::to_string(session_.population.generation + 1), kText, 18.0f);
        const int rated = session_.population.ratedCount();
        const int total = std::max(1, static_cast<int>(session_.population.entries.size()));
        const int round = std::max(1, session_.population.round + 1);
        labelText(content.x, content.y + 58.0f,
                  "Round " + std::to_string(round) + " of " + std::to_string((total + core::kRows - 1) / core::kRows)
                      + "   -   " + std::to_string(rated) + " of " + std::to_string(total) + " rated",
                  kMuted, 12.0f);

        const int playing = static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kStatusPlaying)]));
        const float progress = values_[static_cast<std::size_t>(core::kStatusProgress)];
        const bool clockRunning = values_[static_cast<std::size_t>(core::kStatusClockRunning)] > 0.5f;
        const std::string notice = clockRunning && playing >= 0
            ? "Playing row " + std::to_string(playing + 1) + "  -  MIDI out on channel "
                + std::to_string(static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kMidiChannel)])))
            : "Ready - press a round Play button to send MIDI";
        fillRect({content.x, content.y + 76.0f, content.w, 30.0f}, kSurface2, 8.0f);
        labelText(content.x + 12.0f, content.y + 91.0f, notice, kMuted, 12.0f);

        const float rowTop = content.y + 114.0f;
        for (int position = 0; position < core::kRows; ++position)
            drawRow(content.x, rowTop + static_cast<float>(position) * 76.0f, content.w, position, playing, progress);

        const float actionY = content.y + 498.0f;
        drawButton(kMore, "More", {content.x, actionY, 220.0f, 40.0f}, kSurface, kText, session_.population.canShowMore());
        drawButton(kEvolve, "Evolve", {content.x + 232.0f, actionY, 220.0f, 40.0f}, kAccent, kWhite,
                   session_.population.canEvolve());
        drawButton(kNewPopulation, "New population", {content.x + 464.0f, actionY, 220.0f, 40.0f}, kSurface, kMuted);
        drawButton(kParamBase + core::kLoop,
                   values_[static_cast<std::size_t>(core::kLoop)] > 0.5f ? "Loop: on" : "Loop: off",
                   {content.x + 696.0f, actionY, content.w - 696.0f, 40.0f}, kSurface2, kText);
    }

    void drawRow(const float x, const float y, const float width, const int position,
                 const int playing, const float progress)
    {
        const Rect row {x, y, width, 68.0f};
        const bool isPlaying = playing == position;
        fillRect(row, kSurface, 12.0f);
        strokeRect(row, isPlaying ? kPlay : kLine, isPlaying ? 2.0f : 1.0f, 12.0f);

        const float cx = x + 38.0f;
        const float cy = y + 34.0f;
        beginPath();
        fillColor(kPlay.r, kPlay.g, kPlay.b, kPlay.a);
        circle(cx, cy, 24.0f);
        fill();
        if (isPlaying) {
            fillRect({cx - 7.0f, cy - 7.0f, 14.0f, 14.0f}, kWhite, 2.0f);
        } else {
            beginPath();
            fillColor(kWhite.r, kWhite.g, kWhite.b, kWhite.a);
            moveTo(cx - 6.0f, cy - 9.0f);
            lineTo(cx + 10.0f, cy);
            lineTo(cx - 6.0f, cy + 9.0f);
            closePath();
            fill();
        }
        rememberAction(kPlayBase + position, {cx - 26.0f, cy - 26.0f, 52.0f, 52.0f});

        labelText(x + 78.0f, y + 23.0f, std::to_string(position + 1), kText, 17.0f);
        int entryIndex = -1;
        if (position < static_cast<int>(session_.population.rows.size()))
            entryIndex = session_.population.rows[static_cast<std::size_t>(position)];
        std::string origin = "empty";
        int rating = -2;
        bool carried = false;
        if (entryIndex >= 0 && entryIndex < static_cast<int>(session_.population.entries.size())) {
            const core::Entry& entry = session_.population.entries[static_cast<std::size_t>(entryIndex)];
            origin = entry.carried ? "carried over" : originLabel(entry.genome.origin.c_str());
            rating = static_cast<int>(entry.rating);
            carried = entry.carried;
        }
        labelText(x + 102.0f, y + 23.0f, origin, kMuted, 12.0f);

        const float voteW = 54.0f;
        const float voteGap = 6.0f;
        const float votesX = x + 262.0f;
        static constexpr int ratings[3] = {-1, 0, 1};
        static constexpr const char* voteText[3] = {"-", "0", "+"};
        for (int i = 0; i < 3; ++i) {
            const Rect vote {votesX + static_cast<float>(i) * (voteW + voteGap), y + 16.0f, voteW, 36.0f};
            const bool selected = rating == ratings[i];
            Color face = selected ? (i == 0 ? kDown : i == 1 ? kAccent : kPlay) : kSurface2;
            if (selected && carried)
                face = kWhite;
            fillRect(vote, face, 9.0f);
            strokeRect(vote, selected ? face : kLine, 1.0f, 9.0f);
            labelText(vote.x + vote.w * 0.5f, vote.y + vote.h * 0.5f, voteText[i],
                      selected ? kWhite : kText, 17.0f, ALIGN_CENTER | ALIGN_MIDDLE);
            rememberAction(kVoteBase + position * 3 + i, vote);
        }

        const Rect outBadge {x + width - 66.0f, y + 16.0f, 48.0f, 36.0f};
        fillRect(outBadge, isPlaying ? kPlay : kSurface2, 9.0f);
        labelText(outBadge.x + outBadge.w * 0.5f, outBadge.y + outBadge.h * 0.5f, "OUT",
                  isPlaying ? kWhite : kMuted, 10.0f, ALIGN_CENTER | ALIGN_MIDDLE);

        fillRect({x + 78.0f, y + 56.0f, width - 156.0f, 3.0f}, kSurface2, 2.0f);
        if (isPlaying)
            fillRect({x + 78.0f, y + 56.0f, (width - 156.0f) * std::clamp(progress, 0.0f, 1.0f), 3.0f}, kPlay, 2.0f);
    }

    // ---- Settings ------------------------------------------------------------------------------------------------

    void drawSettings(const Rect content)
    {
        const float gap = 20.0f;
        const float colW = (content.w - gap) * 0.5f;
        const int left[] = {core::kMode, core::kRoot, core::kScale, core::kOctave,
                            core::kBars, core::kTimeSignature, core::kTempo};
        const int right[] = {core::kCrossoverRate, core::kMutationRate, core::kPopulationSize,
                             core::kLoop, core::kMidiChannel, core::kClockSource, core::kSendClock};
        for (int i = 0; i < 7; ++i) {
            drawSetting(static_cast<core::Param>(left[i]), {content.x, content.y + static_cast<float>(i) * 70.0f, colW, 60.0f});
            drawSetting(static_cast<core::Param>(right[i]), {content.x + colW + gap, content.y + static_cast<float>(i) * 70.0f, colW, 60.0f});
        }
        drawButton(kReset, "Reset everything",
                   {content.x, content.y + 500.0f, 240.0f, 38.0f}, kSurface2, kMuted);
        labelText(content.x + 256.0f, content.y + 519.0f,
                  "Changing the line or scale shapes new melodies; existing rows keep their notes.",
                  kMuted, 11.5f);
    }

    void drawSetting(const core::Param parameter, const Rect field)
    {
        strokeRect({field.x, field.y + 58.0f, field.w, 1.0f}, kLine);
        switch (parameter) {
        case core::kMode: {
            drawFieldLabel(field, "Line");
            const float y = field.y + 18.0f;
            const Rect melody {field.x, y, (field.w - 4.0f) * 0.5f, 34.0f};
            const Rect bass {field.x + (field.w + 4.0f) * 0.5f, y, (field.w - 4.0f) * 0.5f, 34.0f};
            const bool isBass = values_[static_cast<std::size_t>(parameter)] > 0.5f;
            fillRect(melody, isBass ? kSurface2 : kSurface, 8.0f);
            fillRect(bass, isBass ? kSurface : kSurface2, 8.0f);
            strokeRect(melody, kLine, 1.0f, 8.0f);
            strokeRect(bass, kLine, 1.0f, 8.0f);
            labelText(melody.x + melody.w * 0.5f, y + 17.0f, "Melody", isBass ? kMuted : kText, 12.5f, ALIGN_CENTER | ALIGN_MIDDLE);
            labelText(bass.x + bass.w * 0.5f, y + 17.0f, "Bass", isBass ? kText : kMuted, 12.5f, ALIGN_CENTER | ALIGN_MIDDLE);
            rememberAction(kSegmentBase, melody);
            rememberAction(kSegmentBase + 1, bass);
            break;
        }
        case core::kRoot: drawSelect(parameter, "Key", field); break;
        case core::kScale: drawSelect(parameter, "Scale", field); break;
        case core::kOctave:
            drawStepper(parameter, "Octave", field, std::to_string(static_cast<int>(std::lround(values_[static_cast<std::size_t>(parameter)]))));
            break;
        case core::kBars:
            drawStepper(parameter, "Bars", field, std::to_string(static_cast<int>(std::lround(values_[static_cast<std::size_t>(parameter)]))));
            break;
        case core::kTimeSignature: drawSelect(parameter, "Time signature", field); break;
        case core::kTempo: {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "%.0f bpm", values_[static_cast<std::size_t>(parameter)]);
            drawSlider(parameter, "Tempo (bpm)", field, buffer);
            break;
        }
        case core::kCrossoverRate: {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(values_[static_cast<std::size_t>(parameter)] * 100.0f)));
            drawSlider(parameter, "Crossover rate", field, buffer);
            break;
        }
        case core::kMutationRate: {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(values_[static_cast<std::size_t>(parameter)] * 100.0f)));
            drawSlider(parameter, "Mutation rate", field, buffer);
            break;
        }
        case core::kPopulationSize:
            drawStepper(parameter, "Population", field, std::to_string(static_cast<int>(std::lround(values_[static_cast<std::size_t>(parameter)]))));
            break;
        case core::kLoop: drawToggle(parameter, "Loop playback", field); break;
        case core::kMidiChannel: drawSelect(parameter, "MIDI output channel", field); break;
        case core::kClockSource: drawSelect(parameter, "Clock source", field); break;
        case core::kSendClock: drawToggle(parameter, "Send MIDI clock", field); break;
        default: break;
        }
    }

    // ---- MIDI / clock ---------------------------------------------------------------------------------------------

    void drawCard(const Rect r, const char* title)
    {
        fillRect(r, kSurface, 12.0f);
        strokeRect(r, kLine, 1.0f, 12.0f);
        fillRect({r.x, r.y, r.w, 38.0f}, kSurface2, 12.0f);
        fillRect({r.x, r.y + 26.0f, r.w, 12.0f}, kSurface2);
        labelText(r.x + 16.0f, r.y + 19.0f, title, kText, 14.0f);
    }

    void infoLine(const float x, const float y, const float width, const std::string& label, const std::string& value)
    {
        labelText(x, y, label, kMuted, 12.0f);
        labelText(x + width, y, value, kText, 12.5f, ALIGN_RIGHT | ALIGN_MIDDLE);
        strokeRect({x, y + 15.0f, width, 1.0f}, kLine);
    }

    void drawOutput(const Rect content)
    {
        const float gap = 16.0f;
        const float cardW = (content.w - gap) * 0.5f;
        const float cardH = 250.0f;
        const Rect a {content.x, content.y + 4.0f, cardW, cardH};
        const Rect b {content.x + cardW + gap, content.y + 4.0f, cardW, cardH};
        const Rect c {content.x, content.y + 20.0f + cardH, cardW, cardH};
        const Rect d {content.x + cardW + gap, content.y + 20.0f + cardH, cardW, cardH};

        const int channel = static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kMidiChannel)]));
        const int lastNote = static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kStatusLastNote)]));
        const int velocity = static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kStatusLastVelocity)]));
        const bool running = values_[static_cast<std::size_t>(core::kStatusClockRunning)] > 0.5f;

        drawCard(a, "MIDI OUTPUT");
        infoLine(a.x + 18.0f, a.y + 64.0f, a.w - 36.0f, "Output channel", "Ch " + std::to_string(channel));
        infoLine(a.x + 18.0f, a.y + 98.0f, a.w - 36.0f, "Last note", withNoteName(lastNote, velocity));
        infoLine(a.x + 18.0f, a.y + 132.0f, a.w - 36.0f, "Playing row",
                 running ? std::to_string(static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kStatusPlaying)])) + 1) : "-");
        labelText(a.x + 18.0f, a.y + 176.0f, "Notes are sent on the chosen channel.", kMuted, 11.5f);
        labelText(a.x + 18.0f, a.y + 196.0f, "Clock messages are system realtime.", kMuted, 11.5f);

        drawCard(b, "CLOCK");
        const int source = static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kClockSource)]));
        infoLine(b.x + 18.0f, b.y + 64.0f, b.w - 36.0f, "Source",
                 core::clockSourceName(static_cast<core::ClockSource>(source)));
        infoLine(b.x + 18.0f, b.y + 98.0f, b.w - 36.0f, "Resolved tempo",
                 std::to_string(static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kStatusClockBpm)]))) + " bpm");
        infoLine(b.x + 18.0f, b.y + 132.0f, b.w - 36.0f, "Transport", running ? "Running" : "Stopped");
        infoLine(b.x + 18.0f, b.y + 166.0f, b.w - 36.0f, "24 PPQ tick",
                 std::to_string(static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kStatusClockTick)]))) + " / 24");
        labelText(b.x + 18.0f, b.y + 208.0f,
                  values_[static_cast<std::size_t>(core::kSendClock)] > 0.5f ? "Outgoing clock: on" : "Outgoing clock: off",
                  kMuted, 11.5f);

        drawCard(c, "RUN STATUS");
        infoLine(c.x + 18.0f, c.y + 64.0f, c.w - 36.0f, "Generation",
                 std::to_string(static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kStatusGeneration)])) + 1));
        infoLine(c.x + 18.0f, c.y + 98.0f, c.w - 36.0f, "Round",
                 std::to_string(static_cast<int>(std::lround(values_[static_cast<std::size_t>(core::kStatusRound)])) + 1));
        infoLine(c.x + 18.0f, c.y + 132.0f, c.w - 36.0f, "Rated",
                 std::to_string(session_.population.ratedCount()) + " / " + std::to_string(session_.population.entries.size()));
        infoLine(c.x + 18.0f, c.y + 166.0f, c.w - 36.0f, "Phrase length",
                 std::to_string(core::genomeLength(session_.settings)) + " steps");
        labelText(c.x + 18.0f, c.y + 208.0f,
                  "Host BBT starts a row on Play; MIDI Start resets and Continue resumes it.",
                  kMuted, 11.0f);

        drawCard(d, "ROUTING");
        static const char* lines[] = {
            "This plugin outputs MIDI only; it produces no audio.",
            "Route its MIDI output to an instrument on another track.",
            "Internal tempo needs no host transport.",
            "MIDI Clock listens for Start, Continue, Stop and 24 PPQ ticks.",
            "Host BBT follows the host tempo and time signature.",
            "Panic (CC 123) is sent whenever playback stops."
        };
        for (int i = 0; i < 6; ++i)
            labelText(d.x + 18.0f, d.y + 66.0f + static_cast<float>(i) * 28.0f, lines[i], kMuted, 11.5f);
    }

    // ---- About ----------------------------------------------------------------------------------------------------

    void drawAbout(const Rect content)
    {
        const Rect card {content.x, content.y + 4.0f, content.w, 442.0f};
        drawCard(card, "About Melevolve");
        static const char* lines[] = {
            "Melevolve is a port of Melogen's evolutionary melody generator.",
            "A genetic algorithm proposes melodies on a sixteenth-note grid. You listen and vote;",
            "the next generation is bred from what you liked, with fresh immigrants for variety.",
            "",
            "What changed for the VST:",
            "  -  Downloaded .mid files are replaced by live MIDI output.",
            "  -  The Instrument tab is now MIDI / Clock, showing the effective output state.",
            "  -  Browser audio, remote instruments and the web server are not part of the plugin.",
            "",
            "Clock sources:",
            "  -  Internal: the tempo slider drives an absolute-time event queue.",
            "  -  MIDI Clock: incoming 24 PPQ ticks drive the step grid; Start, Continue and Stop are obeyed.",
            "  -  Host BBT: the host transport and tempo drive the grid; a Play edge starts the chosen row.",
            "",
            "The session, ratings, population and random-generator state are saved with the project."
        };
        for (int i = 0; i < 16; ++i)
            labelText(card.x + 24.0f, card.y + 66.0f + static_cast<float>(i) * 22.0f, lines[i],
                      i == 4 || i == 10 ? kText : kMuted, i == 4 || i == 10 ? 13.0f : 12.0f);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelevolveUI)
};

UI* createUI() { return new MelevolveUI(); }

END_NAMESPACE_DISTRHO
