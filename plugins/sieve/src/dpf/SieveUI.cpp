#include "downspout/dpf/MagnetoKit.hpp"

#include "sieve_engine.hpp"
#include "sieve_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::sieve;

namespace {

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::kParameterSpecs[i];
            ranges[i] = {s.minimum, s.maximum, (bool)s.integer};
            defaults[i] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

constexpr kit::Accent kAccent {120, 160, 120};

}  // namespace

class SieveUI : public MagnetoUI {
public:
    SieveUI()
        : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
    }

protected:
    void onAction(int /*id*/) override {}

    // The list of choices for each selector, and the panel control that opens it.
    struct Dropdown {
        std::uint32_t parameter = 0;
        Rect box {};
    };

    static int itemCount(const std::uint32_t parameter)
    {
        switch (parameter) {
        case core::kRoot: return 12;
        case core::kScale: return core::kScaleCount;
        case core::kMidiCh: return 17;
        default: return 0;
        }
    }

    static const char* itemText(const std::uint32_t parameter, const int index, char* buffer, const std::size_t size)
    {
        switch (parameter) {
        case core::kRoot: return core::rootName(index);
        case core::kScale: return core::scaleName(index);
        case core::kMidiCh:
            if (index == 0)
                return "All channels";
            std::snprintf(buffer, size, "Ch %d", index);
            return buffer;
        default: return "";
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1 && ev.press) {
            const float mx = static_cast<float>(ev.pos.getX());
            const float my = static_cast<float>(ev.pos.getY());
            if (handleDropdownMouse(mx, my))
                return true;
            if (switchBox_.contains(mx, my)) {
                commit(core::kSource, value(core::kSource) >= 0.5f ? 0.0f : 1.0f);
                return true;
            }
        }
        return MagnetoUI::onMouse(ev);
    }

    void onNanoDisplay() override
    {
        beginFrame();
        dropdownCount_ = 0;
        drawHeader("Sieve", "spectral sieve",
                   "keeps partials near the chosen scale or held notes, in every octave");
        drawSurface({24.0f, 108.0f, (float)getWidth() - 48.0f, (float)getHeight() - 132.0f});

        const Rect panel {24.0f, 108.0f, (float)getWidth() - 48.0f, (float)getHeight() - 132.0f};
        const float gap = 24.0f;
        const float colW = (panel.w - 2.0f * 24.0f - gap) * 0.5f;
        auto cell = [&](int column, int row) {
            return Rect{panel.x + 24.0f + (float)column * (colW + gap),
                        panel.y + 32.0f + (float)row * 84.0f, colW, 40.0f};
        };

        char buf[32];
        drawDropdown(core::kRoot, "Root", cell(0, 0));
        drawDropdown(core::kScale, "Scale", cell(1, 0));
        drawSwitch(core::kSource, "Source", cell(0, 1));
        drawDropdown(core::kMidiCh, "MIDI In", cell(1, 1));
        std::snprintf(buf, sizeof(buf), "%.0f cents", value(core::kWidth));
        drawSlider(core::kWidth, "Width", buf, cell(0, 2), kAccent);
        std::snprintf(buf, sizeof(buf), "%.0f dB", value(core::kFloor));
        drawSlider(core::kFloor, "Floor", buf, cell(1, 2), kAccent);
        std::snprintf(buf, sizeof(buf), "%+.2f", value(core::kTilt));
        drawSlider(core::kTilt, "Tilt", buf, cell(0, 3), kAccent);
        std::snprintf(buf, sizeof(buf), "%.0f%%", value(core::kMix) * 100.0f);
        drawSlider(core::kMix, "Mix", buf, cell(1, 3), kAccent);
        std::snprintf(buf, sizeof(buf), "%+.1f dB", value(core::kLevel));
        drawSlider(core::kLevel, "Level", buf, cell(0, 4), kAccent);

        const float textY = panel.y + panel.h - 76.0f;
        label(panel.x + 24.0f, textY, "Scale follows Root and Scale in every octave.", true, 11.0f);
        label(panel.x + 24.0f, textY + 18.0f,
              "Held MIDI follows notes on the chosen channel; with no note down it falls back to Root and Scale.",
              true, 11.0f);
        label(panel.x + 24.0f, textY + 36.0f,
              "Width is the half-width of each passband in cents; Floor is the minimum gain. Latency is 1024 samples.",
              true, 11.0f);

        drawOpenDropdown();
    }

private:
    void drawSurface(const Rect b)
    {
        const auto& t = theme();
        beginPath();
        fc(t.surface);
        roundedRect(b.x, b.y, b.w, b.h, laf::kRadiusPanel);
        fill();
    }

    void drawDropdown(const std::uint32_t parameter, const char* name, const Rect b)
    {
        const auto& t = theme();
        label(b.x, b.y, name);
        const Rect box {b.x, b.y + 15.0f, b.w, 22.0f};
        const int count = itemCount(parameter);
        const int index = std::clamp(intValue(parameter), 0, count - 1);
        char buffer[32];
        beginPath();
        fc(t.buttonFace);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        stroke();
        fc(t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 8.0f, box.y + box.h * 0.5f, itemText(parameter, index, buffer, sizeof(buffer)), nullptr);
        fc(t.textDim);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 8.0f, box.y + box.h * 0.5f, "v", nullptr);
        if (dropdownCount_ < dropdowns_.size())
            dropdowns_[dropdownCount_++] = {parameter, box};
    }

    // A binary control: the state name on the left, a toggle switch on the right.
    void drawSwitch(const std::uint32_t parameter, const char* name, const Rect b)
    {
        const auto& t = theme();
        label(b.x, b.y, name);
        const Rect box {b.x, b.y + 15.0f, b.w, 22.0f};
        const bool on = value(parameter) >= 0.5f;

        fc(t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x, box.y + box.h * 0.5f, core::kSourceNames[on ? 1 : 0], nullptr);

        const float switchW = 44.0f;
        const float switchH = 20.0f;
        const float x = box.x + box.w - switchW;
        const float y = box.y + (box.h - switchH) * 0.5f;
        beginPath();
        roundedRect(x, y, switchW, switchH, switchH * 0.5f);
        if (on)
            fillColor(kAccent.r, kAccent.g, kAccent.b, 255);
        else
            fc(t.controlTrack);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(x, y, switchW, switchH, switchH * 0.5f);
        stroke();
        beginPath();
        circle(x + (on ? switchW - 11.0f : 11.0f), y + switchH * 0.5f, 7.0f);
        fc(t.controlKnob);
        fill();

        switchBox_ = box;
    }

    // Where the open list goes: below its box when it fits in the window, otherwise above it.
    [[nodiscard]] Rect dropdownPopup(const Dropdown& d) const
    {
        const int count = itemCount(d.parameter);
        const int columns = count > 12 ? 2 : 1;
        const int rows = (count + columns - 1) / columns;
        constexpr float itemH = 24.0f;
        constexpr float columnW = 150.0f;
        const float w = columnW * static_cast<float>(columns);
        const float h = itemH * static_cast<float>(rows) + 8.0f;
        const float x = std::clamp(d.box.x, 8.0f, static_cast<float>(getWidth()) - w - 8.0f);
        const float below = d.box.y + d.box.h + 3.0f;
        const float y = below + h <= static_cast<float>(getHeight()) - 8.0f ? below
                                                                             : std::max(8.0f, d.box.y - 3.0f - h);
        return {x, y, w, h};
    }

    void drawOpenDropdown()
    {
        if (openDropdown_ < 0 || openDropdown_ >= static_cast<int>(dropdownCount_))
            return;
        const auto& t = theme();
        const Dropdown& d = dropdowns_[static_cast<std::size_t>(openDropdown_)];
        const Rect popup = dropdownPopup(d);
        const int count = itemCount(d.parameter);
        const int columns = count > 12 ? 2 : 1;
        const int rows = (count + columns - 1) / columns;
        const float columnW = popup.w / static_cast<float>(columns);
        const int selected = std::clamp(intValue(d.parameter), 0, count - 1);

        beginPath();
        fc(t.surface);
        roundedRect(popup.x, popup.y, popup.w, popup.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(popup.x, popup.y, popup.w, popup.h, laf::kRadiusSmall);
        stroke();

        char buffer[32];
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < count; ++i) {
            const Rect item {popup.x + static_cast<float>(i / rows) * columnW + 4.0f,
                             popup.y + 4.0f + static_cast<float>(i % rows) * 24.0f, columnW - 8.0f, 24.0f};
            if (i == selected) {
                beginPath();
                fillColor(kAccent.r, kAccent.g, kAccent.b, 255);
                roundedRect(item.x, item.y + 1.0f, item.w, item.h - 2.0f, 4.0f);
                fill();
                fillColor(250, 248, 242, 255);
            } else {
                fc(t.textPrimary);
            }
            text(item.x + 8.0f, item.y + item.h * 0.5f, itemText(d.parameter, i, buffer, sizeof(buffer)), nullptr);
        }
    }

    // A click while a list is open picks an item or closes it; a click on a closed box opens its list.
    bool handleDropdownMouse(const float mx, const float my)
    {
        if (openDropdown_ >= 0 && openDropdown_ < static_cast<int>(dropdownCount_)) {
            const Dropdown d = dropdowns_[static_cast<std::size_t>(openDropdown_)];
            const Rect popup = dropdownPopup(d);
            openDropdown_ = -1;
            if (popup.contains(mx, my)) {
                const int count = itemCount(d.parameter);
                const int columns = count > 12 ? 2 : 1;
                const int rows = (count + columns - 1) / columns;
                const float columnW = popup.w / static_cast<float>(columns);
                const int column = std::clamp(static_cast<int>((mx - popup.x) / columnW), 0, columns - 1);
                const int row = std::clamp(static_cast<int>((my - popup.y - 4.0f) / 24.0f), 0, rows - 1);
                const int index = column * rows + row;
                if (index < count)
                    commit(d.parameter, static_cast<float>(index));
            }
            repaint();
            return true;  // the click that closes a list does nothing else
        }
        for (std::size_t i = 0; i < dropdownCount_; ++i) {
            if (dropdowns_[i].box.contains(mx, my)) {
                openDropdown_ = static_cast<int>(i);
                repaint();
                return true;
            }
        }
        return false;
    }

    std::array<Dropdown, 4> dropdowns_ {};
    std::size_t dropdownCount_ = 0;
    int openDropdown_ = -1;
    Rect switchBox_ {};
};

UI* createUI() { return new SieveUI(); }

END_NAMESPACE_DISTRHO
