#include "MagnetoKit.hpp"

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

constexpr const char* kLearnNames[2] = {"Off", "On"};

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
        if (MagnetoUI::onMouse(ev))
            return true;
        if (ev.button != 1 || !ev.press)
            return false;
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        for (int from = 0; from < core::kStates; ++from) {
            for (int to = 0; to < core::kStates; ++to) {
                if (cells_[static_cast<std::size_t>(from * core::kStates + to)].contains(mx, my)) {
                    core::bumpCell(model_, from, to);
                    pushEdit();
                    repaint();
                    return true;
                }
            }
        }
        return false;
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("Markov", "Markov-chain melody generator",
                   "each note is chosen from the row of the matrix for the note before it. Draw the matrix, pick a style, or learn it from a line you play.");
        drawMatrixPanel({24.0f, 108.0f, 912.0f, 482.0f});

        const float y = 604.0f, h = 264.0f, gap = 12.0f;
        const float w = (912.0f - 2.0f * gap) / 3.0f;
        drawMelodyPanel({24.0f, y, w, h});
        drawTimingPanel({24.0f + w + gap, y, w, h});
        drawLearnPanel({24.0f + 2.0f * (w + gap), y, w, h});
    }

private:
    core::Model model_ {};
    std::array<Rect, core::kCells> cells_ {};
    std::uint32_t randomSerial_ = 0;

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
        drawPanel(b, "TRANSITIONS", kMatrixAccent, "row = the note just played, column = the note that may follow; click a cell to change its weight (0-8)");
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

    // ---- Controls --------------------------------------------------------------------------------------------

    void slider(const std::uint32_t param, const char* name, const char* shown, const Rect panel, const int column, const int row, const Accent accent)
    {
        const float colW = (panel.w - 28.0f - 12.0f) * 0.5f;
        drawSlider(param, name, shown, {panel.x + 14.0f + static_cast<float>(column) * (colW + 12.0f), panel.y + 46.0f + static_cast<float>(row) * 56.0f, colW, 40.0f}, accent);
    }

    void drawMelodyPanel(const Rect b)
    {
        drawPanel(b, "MELODY", kMelodyAccent);
        const float colW = (b.w - 28.0f - 12.0f) * 0.5f;
        const std::string root = noteName(intValue(core::kRoot));
        slider(core::kRoot, "Root note", root.c_str(), b, 0, 0, kMelodyAccent);
        drawStepper(core::kScale, "Scale", core::scaleName(intValue(core::kScale)), {b.x + 14.0f, b.y + 46.0f + 56.0f, colW, 40.0f});
        char orderText[8];
        std::snprintf(orderText, sizeof(orderText), "%d", intValue(core::kOrder));
        drawStepper(core::kOrder, "Order (notes of history)", orderText, {b.x + 14.0f, b.y + 46.0f + 112.0f, colW, 40.0f});

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kChaos) * 100.0f)));
        slider(core::kChaos, "Chaos", buf, b, 1, 0, kMelodyAccent);
        const std::string low = noteName(intValue(core::kLow));
        slider(core::kLow, "Lowest note", low.c_str(), b, 1, 1, kMelodyAccent);
        std::snprintf(buf, sizeof(buf), "%d", intValue(core::kOctaves));
        slider(core::kOctaves, "Octaves", buf, b, 1, 2, kMelodyAccent);
    }

    void drawTimingPanel(const Rect b)
    {
        drawPanel(b, "TIMING AND DYNAMICS", kTimingAccent);
        const float colW = (b.w - 28.0f - 12.0f) * 0.5f;
        char buf[32];
        drawStepper(core::kGrid, "Step grid", core::gridName(intValue(core::kGrid)), {b.x + 14.0f, b.y + 46.0f, colW, 40.0f});
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kGate) * 100.0f)));
        slider(core::kGate, "Gate", buf, b, 0, 1, kTimingAccent);
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kDensity) * 100.0f)));
        slider(core::kDensity, "Density", buf, b, 0, 2, kTimingAccent);

        std::snprintf(buf, sizeof(buf), "%d", intValue(core::kVelocity));
        slider(core::kVelocity, "Velocity", buf, b, 1, 0, kTimingAccent);
        std::snprintf(buf, sizeof(buf), "%d bar%s", intValue(core::kPhraseBars), intValue(core::kPhraseBars) == 1 ? "" : "s");
        slider(core::kPhraseBars, "Phrase length", buf, b, 1, 1, kTimingAccent);
        std::snprintf(buf, sizeof(buf), "%d", intValue(core::kSeed));
        slider(core::kSeed, "Seed", buf, b, 1, 2, kTimingAccent);
        std::snprintf(buf, sizeof(buf), "Ch %d", intValue(core::kChannel));
        drawSlider(core::kChannel, "MIDI channel", buf, {b.x + 14.0f, b.y + 46.0f + 168.0f, b.w - 28.0f, 40.0f}, kTimingAccent);
    }

    void drawLearnPanel(const Rect b)
    {
        drawPanel(b, "LEARN AND CONDUCTOR", kLearnAccent);
        const float colW = (b.w - 28.0f - 12.0f) * 0.5f;
        label(b.x + 14.0f, b.y + 46.0f, "Learn from MIDI input", true, 12.0f);
        drawSegments(core::kLearn, kLearnNames, 2, {b.x + 14.0f, b.y + 64.0f, colW, 26.0f}, kLearnAccent);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%u transition%s heard", static_cast<unsigned>(model_.learnedTotal), model_.learnedTotal == 1 ? "" : "s");
        label(b.x + 14.0f + colW + 12.0f, b.y + 70.0f, buf, true, 11.0f);

        if (intValue(core::kLearnChannel) == 0)
            std::snprintf(buf, sizeof(buf), "all");
        else
            std::snprintf(buf, sizeof(buf), "Ch %d", intValue(core::kLearnChannel));
        slider(core::kLearnChannel, "Learn channel", buf, b, 0, 1, kLearnAccent);
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kLearnedMix) * 100.0f)));
        slider(core::kLearnedMix, "Learned mix", buf, b, 1, 1, kLearnAccent);
        drawButton(kActionClearLearned, "Clear learned", {b.x + 14.0f, b.y + 46.0f + 112.0f, colW, 30.0f}, kLearnAccent, false);

        if (intValue(core::kConductorCh) == 0)
            std::snprintf(buf, sizeof(buf), "off");
        else
            std::snprintf(buf, sizeof(buf), "Ch %d", intValue(core::kConductorCh));
        drawSlider(core::kConductorCh, "Conductor ch", buf, {b.x + 14.0f + colW + 12.0f, b.y + 46.0f + 108.0f, colW, 40.0f}, kConductorAccent);
        label(b.x + 14.0f, b.y + 46.0f + 160.0f, "Conductor: CC 21 density, 22 velocity, 23 chaos,", true, 10.5f);
        label(b.x + 14.0f, b.y + 46.0f + 174.0f, "24 = 127 re-rolls the melody at the next bar.", true, 10.5f);
    }
};

UI* createUI() { return new MarkovUI(); }

END_NAMESPACE_DISTRHO
