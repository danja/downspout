#include "downspout/dpf/MagnetoKit.hpp"

#include "markov_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::markov;

namespace {

constexpr kit::Accent kMatrixAccent {198, 120, 64};   // copper
constexpr kit::Accent kMelodyAccent {94, 158, 112};   // leaf green
constexpr kit::Accent kTimingAccent {198, 132, 58};   // amber
constexpr kit::Accent kLearnAccent {92, 140, 156};    // cold steel
constexpr kit::Accent kConductorAccent {120, 126, 170};  // slate violet

constexpr const char* kStateKeyModel = "model";
constexpr const char* kStateKeyEdit = "edit";


// Button ids: 0-7 load a style, then the editing actions.
constexpr int kActionClear = 100;
constexpr int kActionRandomise = 101;
constexpr int kActionClearLearned = 102;

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::kParameterSpecs[i];
            ranges[i] = {s.minimum, s.maximum, s.integer};
            defaults[i] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

const char* pitchClassName(const int pc)
{
    static constexpr const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return kNames[(pc % 12 + 12) % 12];
}

std::string noteName(const int midi)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%s%d (%d)", pitchClassName(midi), midi / 12 - 1, midi);
    return buffer;
}

}  // namespace

class MarkovUI : public MagnetoUI {
public:
    MarkovUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) { model_ = core::defaultModel(); }

protected:
    void onAction(const int id) override
    {
        if (id >= 0 && id < core::kStyleCount) {
            core::loadStyle(model_, id);
            pushEdit();
        } else if (id == kActionClear) {
            core::clearBase(model_);
            pushEdit();
        } else if (id == kActionRandomise) {
            core::randomiseBase(model_, 0x4d61726bull + (++randomSerial_) * 7919ull);
            pushEdit();
        } else if (id == kActionClearLearned) {
            // A counter: any change tells the plugin to wipe what it has learned.
            commit(core::kClearLearned, std::fmod(value(core::kClearLearned) + 1.0f, 65536.0f));
            core::clearLearned(model_);
        }
        repaint();
    }

    void stateChanged(const char* key, const char* text) override
    {
        if (key == nullptr || text == nullptr || std::string(key) != kStateKeyModel)
            return;
        if (const auto model = core::deserializeModel(text)) {
            model_ = *model;
            repaint();
        }
    }

    // The matrix cells are not kit controls, so they are handled here once the kit has had its turn.
    bool onMouse(const MouseEvent& ev) override
    {
        if (!ev.press) {
            painting_ = false;
            return MagnetoUI::onMouse(ev);
        }
        // Right-click zeroes a cell.
        if (ev.button == 3) {
            const int cell = cellAt(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
            if (cell < 0) return false;
            core::setCell(model_, cell / core::kStates, cell % core::kStates, 0);
            pushEdit();
            repaint();
            return true;
        }
        if (ev.button == 1 && handleDropdownMouse(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY())))
            return true;
        if (MagnetoUI::onMouse(ev))
            return true;
        if (ev.button != 1)
            return false;
        // A click cycles the cell's weight; dragging on from there paints that weight into the cells it crosses.
        const int cell = cellAt(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
        if (cell < 0)
            return false;
        core::bumpCell(model_, cell / core::kStates, cell % core::kStates);
        paintWeight_ = model_.base[static_cast<std::size_t>(cell)];
        paintedCell_ = cell;
        painting_ = true;
        pushEdit();
        repaint();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (MagnetoUI::onMotion(ev))
            return true;
        if (!painting_)
            return false;
        const int cell = cellAt(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
        if (cell < 0 || cell == paintedCell_)
            return true;
        paintedCell_ = cell;
        core::setCell(model_, cell / core::kStates, cell % core::kStates, paintWeight_);
        pushEdit();
        repaint();
        return true;
    }

    void onNanoDisplay() override
    {
        beginFrame();
        dropdownCount_ = 0;
        drawHeader("Markov", "Markov-chain melody generator",
                   "each note is chosen from the row of the matrix for the note before it. Draw the matrix, pick a style, or learn it from a line you play.");
        drawMatrixPanel({24.0f, 108.0f, 912.0f, 482.0f});

        const float y = 604.0f, h = 264.0f, gap = 12.0f;
        const float w = (912.0f - 2.0f * gap) / 3.0f;
        drawMelodyPanel({24.0f, y, w, h});
        drawTimingPanel({24.0f + w + gap, y, w, h});
        drawLearnPanel({24.0f + 2.0f * (w + gap), y, w, h});
        drawOpenDropdown();
    }

private:
    core::Model model_ {};
    std::array<Rect, core::kCells> cells_ {};
    std::uint32_t randomSerial_ = 0;

    // Painting the matrix: a click sets the weight, then dragging copies it into the cells crossed.
    bool painting_ = false;
    int paintWeight_ = 0;
    int paintedCell_ = -1;

    [[nodiscard]] int cellAt(const float mx, const float my) const
    {
        for (int i = 0; i < core::kCells; ++i)
            if (cells_[static_cast<std::size_t>(i)].contains(mx, my))
                return i;
        return -1;
    }

    // Drop-down lists for every choice. The kit has none, so they live here (the kit copies stay identical).
    struct Dropdown {
        std::uint32_t parameter = 0;
        Rect box {};
    };
    std::array<Dropdown, 16> dropdowns_ {};
    std::size_t dropdownCount_ = 0;
    int openDropdown_ = -1;  // index into dropdowns_, or -1

    [[nodiscard]] core::Params currentParams() const
    {
        core::Params p {};
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            p[i] = value(i);
        return p;
    }

    // Sends the matrix on screen to the plugin. Only the matrix goes, so a melody being learned meanwhile is not overwritten.
    void pushEdit()
    {
        std::string text(core::kCells, '0');
        for (int i = 0; i < core::kCells; ++i)
            text[static_cast<std::size_t>(i)] = static_cast<char>('0' + model_.base[static_cast<std::size_t>(i)]);
        setState(kStateKeyEdit, text.c_str());
    }

    bool baseMatches(const int style) const
    {
        core::Model reference;
        core::loadStyle(reference, style);
        return reference.base == model_.base;
    }

    // ---- The matrix ------------------------------------------------------------------------------------------

    void drawMatrixPanel(const Rect b)
    {
        drawPanel(b, "TRANSITIONS", kMatrixAccent, "row = the note just played, column = the note that may follow; click to change a weight (0-8), drag to paint, right-click to zero");
        const auto& t = theme();

        const core::Params params = currentParams();
        const core::Matrix matrix = core::effectiveMatrix(model_, params);
        const std::uint16_t mask = core::scaleMask(intValue(core::kScale));
        const int rootPc = ((intValue(core::kRoot) % 12) + 12) % 12;
        const int current = intValue(core::kStatusState);

        constexpr float cell = 30.0f;
        constexpr float step = 32.0f;
        const float x0 = b.x + 74.0f;
        const float y0 = b.y + 84.0f;

        label(x0, b.y + 40.0f, "next note", true, 11.0f);
        label(b.x + 14.0f, y0 - 24.0f, "last note", true, 11.0f);

        // Column and row labels: the pitch classes, counted from the Root note.
        for (int i = 0; i < core::kStates; ++i) {
            const bool inScale = ((mask >> i) & 1u) != 0;
            const char* name = pitchClassName(rootPc + i);
            label(x0 + static_cast<float>(i) * step + cell * 0.5f, y0 - 18.0f, name, !inScale, 11.0f, ALIGN_CENTER | ALIGN_TOP);
            label(x0 - 10.0f, y0 + static_cast<float>(i) * step + cell * 0.5f, name, !inScale, 11.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        }

        for (int from = 0; from < core::kStates; ++from) {
            float total = 0.0f;
            for (int to = 0; to < core::kStates; ++to)
                total += matrix[static_cast<std::size_t>(from * core::kStates + to)];
            for (int to = 0; to < core::kStates; ++to) {
                const std::size_t index = static_cast<std::size_t>(from * core::kStates + to);
                const Rect r {x0 + static_cast<float>(to) * step, y0 + static_cast<float>(from) * step, cell, cell};
                cells_[index] = r;

                const bool inScale = ((mask >> to) & 1u) != 0;
                const float probability = total > 0.0f ? matrix[index] / total : 0.0f;
                beginPath();
                fc(t.controlTrack);
                roundedRect(r.x, r.y, r.w, r.h, 4.0f);
                fill();
                if (probability > 0.0f) {
                    beginPath();
                    fillColor(kMatrixAccent.r, kMatrixAccent.g, kMatrixAccent.b,
                              static_cast<unsigned char>(std::clamp(40.0f + 215.0f * std::sqrt(probability), 0.0f, 255.0f)));
                    roundedRect(r.x, r.y, r.w, r.h, 4.0f);
                    fill();
                }
                if (!inScale) {
                    // Not in the scale: it can never be chosen, whatever its weight.
                    beginPath();
                    fillColor(0, 0, 0, 70);
                    roundedRect(r.x, r.y, r.w, r.h, 4.0f);
                    fill();
                }
                const int weight = model_.base[index];
                if (weight > 0) {
                    char digit[2] = {static_cast<char>('0' + weight), 0};
                    fillColor(probability > 0.28f ? 250 : 200, probability > 0.28f ? 248 : 196, probability > 0.28f ? 242 : 188, inScale ? 230 : 110);
                    fontSize(11.0f);
                    textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                    text(r.x + r.w * 0.5f, r.y + r.h * 0.5f + 1.0f, digit, nullptr);
                }
            }
        }

        // The row the next note will be drawn from.
        if (current >= 0 && current < core::kStates) {
            beginPath();
            sc(t.textPrimary);
            strokeWidth(1.5f);
            roundedRect(x0 - 3.0f, y0 + static_cast<float>(current) * step - 3.0f, step * core::kStates - 2.0f + 6.0f, cell + 6.0f, 6.0f);
            stroke();
        }

        drawMatrixControls({x0 + step * core::kStates + 36.0f, b.y + 48.0f, b.x + b.w - (x0 + step * core::kStates + 36.0f) - 16.0f, b.h - 60.0f});
    }

    void drawMatrixControls(const Rect b)
    {
        label(b.x, b.y, "Style (replaces the matrix)", true, 12.0f);
        const float buttonW = (b.w - 8.0f) * 0.5f;
        for (int i = 0; i < core::kStyleCount; ++i) {
            const Rect r {b.x + static_cast<float>(i % 2) * (buttonW + 8.0f), b.y + 22.0f + static_cast<float>(i / 2) * 40.0f, buttonW, 32.0f};
            drawButton(i, core::styleName(i), r, kMatrixAccent, baseMatches(i));
        }

        const float rowY = b.y + 22.0f + 4.0f * 40.0f + 12.0f;
        drawButton(kActionClear, "Clear matrix", {b.x, rowY, buttonW, 32.0f}, kMatrixAccent, false);
        drawButton(kActionRandomise, "Randomise", {b.x + buttonW + 8.0f, rowY, buttonW, 32.0f}, kMatrixAccent, false);

        const float textY = rowY + 52.0f;
        label(b.x, textY, "Brightness is the chance, after the scale, what", true, 11.0f);
        label(b.x, textY + 15.0f, "was learned and Chaos. The digit is the weight", true, 11.0f);
        label(b.x, textY + 30.0f, "you set. Dimmed columns are outside the scale.", true, 11.0f);
        label(b.x, textY + 52.0f, "The outlined row is the one in use right now.", true, 11.0f);
        label(b.x, textY + 67.0f, "An empty row plays any note in the scale.", true, 11.0f);
    }

    // ---- Drop-down lists ---------------------------------------------------------------------------------------

    static int itemCount(const std::uint32_t parameter)
    {
        switch (parameter) {
        case core::kScale: return core::kScaleCount;
        case core::kGrid: return core::kGridCount;
        case core::kOrder: return 2;
        case core::kLearn: return 2;
        case core::kLearnChannel: return 17;
        case core::kConductorCh: return 17;
        case core::kChannel: return 16;
        default: return 0;
        }
    }

    // The parameter value of the first item (Order and Channel start at 1, the others at 0).
    static int itemOffset(const std::uint32_t parameter)
    {
        return parameter == core::kOrder || parameter == core::kChannel ? 1 : 0;
    }

    static const char* itemText(const std::uint32_t parameter, const int index, char* buffer, const std::size_t size)
    {
        switch (parameter) {
        case core::kScale: return core::scaleName(index);
        case core::kGrid: return core::gridName(index);
        case core::kOrder: return index == 0 ? "1 note back" : "2 notes back";
        case core::kLearn: return index == 0 ? "Off" : "On";
        case core::kLearnChannel:
            if (index == 0) return "All channels";
            break;
        case core::kConductorCh:
            if (index == 0) return "Off";
            break;
        default: break;
        }
        const int channel = parameter == core::kChannel ? index + 1 : index;
        std::snprintf(buffer, size, "Ch %d", channel);
        return buffer;
    }

    void drawDropdown(const std::uint32_t parameter, const char* name, const Rect b)
    {
        const auto& t = theme();
        label(b.x, b.y, name);
        const Rect box {b.x, b.y + 15.0f, b.w, 22.0f};
        const int index = std::clamp(intValue(parameter) - itemOffset(parameter), 0, itemCount(parameter) - 1);
        char buffer[16];
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

    // Where the open list goes: below its box when it fits in the window, otherwise above it.
    [[nodiscard]] Rect dropdownPopup(const Dropdown& d) const
    {
        const int count = itemCount(d.parameter);
        const int columns = count > 12 ? 2 : 1;
        const int rows = (count + columns - 1) / columns;
        constexpr float itemH = 24.0f;
        const float columnW = d.parameter == core::kScale ? 138.0f : std::max(d.box.w, 112.0f);
        const float w = columnW * static_cast<float>(columns);
        const float h = itemH * static_cast<float>(rows) + 8.0f;
        const float x = std::clamp(d.box.x, 8.0f, static_cast<float>(getWidth()) - w - 8.0f);
        const float below = d.box.y + d.box.h + 3.0f;
        const float y = below + h <= static_cast<float>(getHeight()) - 8.0f ? below : std::max(8.0f, d.box.y - 3.0f - h);
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
        const int selected = intValue(d.parameter) - itemOffset(d.parameter);

        beginPath();
        fc(t.surface);
        roundedRect(popup.x, popup.y, popup.w, popup.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(popup.x, popup.y, popup.w, popup.h, laf::kRadiusSmall);
        stroke();

        char buffer[16];
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < count; ++i) {
            const Rect item {popup.x + static_cast<float>(i / rows) * columnW + 4.0f, popup.y + 4.0f + static_cast<float>(i % rows) * 24.0f,
                             columnW - 8.0f, 24.0f};
            if (i == selected) {
                beginPath();
                fillColor(kMatrixAccent.r, kMatrixAccent.g, kMatrixAccent.b, 255);
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
                    commit(d.parameter, static_cast<float>(index + itemOffset(d.parameter)));
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

    // ---- Controls --------------------------------------------------------------------------------------------

    // A grid cell of a panel: column 0 or 1, row from the top.
    [[nodiscard]] static Rect cellOf(const Rect panel, const int column, const int row)
    {
        const float colW = (panel.w - 28.0f - 12.0f) * 0.5f;
        return {panel.x + 14.0f + static_cast<float>(column) * (colW + 12.0f), panel.y + 46.0f + static_cast<float>(row) * 56.0f, colW, 40.0f};
    }

    void drawMelodyPanel(const Rect b)
    {
        drawPanel(b, "MELODY", kMelodyAccent);
        char buf[32];
        const std::string root = noteName(intValue(core::kRoot));
        drawSlider(core::kRoot, "Root note", root.c_str(), cellOf(b, 0, 0), kMelodyAccent);
        drawDropdown(core::kScale, "Scale", cellOf(b, 0, 1));
        drawDropdown(core::kOrder, "Order (notes of history)", cellOf(b, 0, 2));

        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kChaos) * 100.0f)));
        drawSlider(core::kChaos, "Chaos", buf, cellOf(b, 1, 0), kMelodyAccent);
        const std::string low = noteName(intValue(core::kLow));
        drawSlider(core::kLow, "Lowest note", low.c_str(), cellOf(b, 1, 1), kMelodyAccent);
        std::snprintf(buf, sizeof(buf), "%d", intValue(core::kOctaves));
        drawSlider(core::kOctaves, "Octaves", buf, cellOf(b, 1, 2), kMelodyAccent);
    }

    void drawTimingPanel(const Rect b)
    {
        drawPanel(b, "TIMING AND DYNAMICS", kTimingAccent);
        char buf[32];
        drawDropdown(core::kGrid, "Step grid", cellOf(b, 0, 0));
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kGate) * 100.0f)));
        drawSlider(core::kGate, "Gate", buf, cellOf(b, 0, 1), kTimingAccent);
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kDensity) * 100.0f)));
        drawSlider(core::kDensity, "Density", buf, cellOf(b, 0, 2), kTimingAccent);

        std::snprintf(buf, sizeof(buf), "%d", intValue(core::kVelocity));
        drawSlider(core::kVelocity, "Velocity", buf, cellOf(b, 1, 0), kTimingAccent);
        std::snprintf(buf, sizeof(buf), "%d bar%s", intValue(core::kPhraseBars), intValue(core::kPhraseBars) == 1 ? "" : "s");
        drawSlider(core::kPhraseBars, "Phrase length", buf, cellOf(b, 1, 1), kTimingAccent);
        std::snprintf(buf, sizeof(buf), "%d", intValue(core::kSeed));
        drawSlider(core::kSeed, "Seed", buf, cellOf(b, 1, 2), kTimingAccent);

        Rect channel = cellOf(b, 0, 3);
        channel.w = b.w - 28.0f;
        drawDropdown(core::kChannel, "MIDI output channel", channel);
    }

    void drawLearnPanel(const Rect b)
    {
        drawPanel(b, "LEARN AND CONDUCTOR", kLearnAccent);
        char buf[48];
        drawDropdown(core::kLearn, "Learn from MIDI input", cellOf(b, 0, 0));
        std::snprintf(buf, sizeof(buf), "%u transition%s heard", static_cast<unsigned>(model_.learnedTotal), model_.learnedTotal == 1 ? "" : "s");
        const Rect heard = cellOf(b, 1, 0);
        label(heard.x, heard.y + 19.0f, buf, true, 11.0f);

        drawDropdown(core::kLearnChannel, "Learn channel", cellOf(b, 0, 1));
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kLearnedMix) * 100.0f)));
        drawSlider(core::kLearnedMix, "Learned mix", buf, cellOf(b, 1, 1), kLearnAccent);

        const Rect clear = cellOf(b, 0, 2);
        drawButton(kActionClearLearned, "Clear learned", {clear.x, clear.y + 6.0f, clear.w, 30.0f}, kLearnAccent, false);
        drawDropdown(core::kConductorCh, "Conductor ch", cellOf(b, 1, 2));

        label(b.x + 14.0f, b.y + 46.0f + 3.0f * 56.0f, "Conductor: CC 21 density, 22 velocity, 23 chaos,", true, 10.5f);
        label(b.x + 14.0f, b.y + 46.0f + 3.0f * 56.0f + 14.0f, "24 = 127 re-rolls the melody at the next bar.", true, 10.5f);
    }
};

UI* createUI() { return new MarkovUI(); }

END_NAMESPACE_DISTRHO
