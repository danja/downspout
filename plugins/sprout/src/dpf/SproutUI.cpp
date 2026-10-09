#include "MagnetoKit.hpp"

#include "sprout_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::sprout;

namespace {

constexpr kit::Accent kGrammarAccent {94, 158, 112};  // leaf green
constexpr kit::Accent kPatternAccent {92, 140, 156};  // cold steel
constexpr kit::Accent kPitchAccent {176, 112, 62};    // copper
constexpr kit::Accent kTimeAccent {198, 132, 58};     // amber

constexpr kit::Accent kMidiAccent {120, 126, 170};    // slate violet

constexpr const char* kPitchSourceNames[3] = {"Scale", "Held", "Latched"};

constexpr const char* kPresetNames[core::kPresetCount] = {"Plant", "Koch", "Dragon", "Sierpinski", "Cantor", "Levy", "Tree"};

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

std::string noteName(const int midi)
{
    static constexpr const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%s%d (%d)", kNames[(midi % 12 + 12) % 12], midi / 12 - 1, midi);
    return buffer;
}

}  // namespace

class SproutUI : public MagnetoUI {
public:
    SproutUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) { core::prepare(); }

protected:
    void onAction(int) override {}

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        beginFrame();
        drawHeader("Sprout", "L-system melody generator",
                   "a grammar is rewritten each generation, then the string is played: F note, f rest, + - pitch, [ ] branch.");
        drawGrammarPanel({24.0f, 108.0f, width - 48.0f, 118.0f});
        drawPatternPanel({24.0f, 238.0f, width - 48.0f, 168.0f});

        const float y = 418.0f, h = 224.0f, gap = 16.0f;
        const float w = (width - 48.0f - 2.0f * gap) / 3.0f;
        drawGrowthPanel({24.0f, y, w, h});
        drawPitchPanel({24.0f + w + gap, y, w, h});
        drawTimingPanel({24.0f + 2.0f * (w + gap), y, w, h});
        drawMidiPanel({24.0f, y + h + gap, width - 48.0f, height - (y + h + gap) - 18.0f});
    }

private:
    int preset() const { return intValue(core::kPreset); }

    void drawGrammarPanel(const Rect b)
    {
        drawPanel(b, "GRAMMAR", kGrammarAccent, "each symbol is rewritten once per generation");
        drawSegments(core::kPreset, kPresetNames, core::kPresetCount, {b.x + 14.0f, b.y + 44.0f, b.w - 28.0f, 30.0f}, kGrammarAccent);
        label(b.x + 16.0f, b.y + 84.0f, "Rules");
        label(b.x + 66.0f, b.y + 84.0f, core::presetRules(preset()), false);
        label(b.x + 16.0f, b.y + 98.0f, "F, G note    f rest    + - pitch up or down    [ ] branch    other letters only grow", true, 10.5f);
    }

    void drawPatternPanel(const Rect b)
    {
        const int shown = value(core::kStatusGeneration) > 0.5f ? intValue(core::kStatusGeneration) : intValue(core::kGenerations);
        const core::Sequence& seq = core::sequenceFor(preset(), shown);
        char right[96];
        std::snprintf(right, sizeof(right), "generation %d  -  %d steps  -  bright column is the playhead", seq.generation,
                      static_cast<int>(seq.steps.size()));
        drawPanel(b, "PATTERN", kPatternAccent, right);

        const Rect plot {b.x + 14.0f, b.y + 44.0f, b.w - 28.0f, b.h - 58.0f};
        drawPlotFrame(plot);
        if (seq.steps.empty()) {
            label(plot.x + plot.w * 0.5f, plot.y + plot.h * 0.5f, "Generation 0 is only variables: nothing sounds yet", true, 12.0f,
                  ALIGN_CENTER | ALIGN_MIDDLE);
            return;
        }

        const int stepSize = intValue(core::kStepSize);
        const int range = intValue(core::kRange);
        int lo = range, hi = -range;
        for (const core::Step& s : seq.steps) {
            if (!s.note) continue;
            const int degree = core::foldDegree(s.unit * stepSize, range);
            lo = std::min(lo, degree);
            hi = std::max(hi, degree);
        }
        if (hi < lo) lo = hi = 0;
        const float span = static_cast<float>(std::max(hi - lo, 6));
        const float mid = 0.5f * static_cast<float>(hi + lo);

        const float total = static_cast<float>(seq.steps.size());
        const float inner = plot.w - 12.0f;
        const float cell = std::max(1.5f, inner / total);
        const int playhead = intValue(core::kStatusStep);
        const auto& t = theme();
        const auto yOf = [&](const int degree) {
            const float norm = 0.5f + 0.40f * (static_cast<float>(degree) - mid) / (0.5f * span);
            return plot.y + plot.h - 6.0f - norm * (plot.h - 12.0f);
        };
        if (inner / total >= 1.5f) {
            for (std::size_t i = 0; i < seq.steps.size(); ++i) {
                const core::Step& s = seq.steps[i];
                if (!s.note) continue;
                const float px = plot.x + 6.0f + static_cast<float>(i) / total * inner;
                beginPath();
                if (static_cast<int>(i) == playhead)
                    fc(t.textPrimary);
                else
                    fillColor(kPatternAccent.r, kPatternAccent.g, kPatternAccent.b, 255 - std::min(150, s.depth * 45));
                rect(px, yOf(core::foldDegree(s.unit * stepSize, range)) - 1.5f,
                     std::max(1.5f, cell - (cell > 4.0f ? 1.0f : 0.0f)), 3.0f);
                fill();
            }
        } else {
            // More steps than pixels: one bar per 1.5 px column spanning the pitch range
            // of the notes in it, so a pattern of 100,000 steps is still a few hundred rects.
            const int columns = std::max(1, static_cast<int>(inner / 1.5f));
            for (int col = 0; col < columns; ++col) {
                const std::size_t from = static_cast<std::size_t>(static_cast<double>(col) / columns * total);
                const std::size_t to = std::min(seq.steps.size(), static_cast<std::size_t>(static_cast<double>(col + 1) / columns * total) + 1);
                int lowDegree = range + 1, highDegree = -range - 1;
                for (std::size_t i = from; i < to; ++i) {
                    if (!seq.steps[i].note) continue;
                    const int degree = core::foldDegree(seq.steps[i].unit * stepSize, range);
                    lowDegree = std::min(lowDegree, degree);
                    highDegree = std::max(highDegree, degree);
                }
                if (highDegree < lowDegree) continue;
                const float top = yOf(highDegree) - 1.5f, bottom = yOf(lowDegree) + 1.5f;
                beginPath();
                fillColor(kPatternAccent.r, kPatternAccent.g, kPatternAccent.b, 200);
                rect(plot.x + 6.0f + static_cast<float>(col) * inner / static_cast<float>(columns), top, 1.5f, bottom - top);
                fill();
            }
        }
        beginPath();
        fillColor(kPatternAccent.r, kPatternAccent.g, kPatternAccent.b, 60);
        rect(plot.x + 6.0f + static_cast<float>(playhead) / total * inner, plot.y + 4.0f, std::max(1.5f, cell), plot.h - 8.0f);
        fill();
    }

    void slider(const std::uint32_t param, const char* name, const char* shown, const Rect panel, const int row, const Accent accent)
    {
        drawSlider(param, name, shown, {panel.x + 14.0f, panel.y + 46.0f + static_cast<float>(row) * 42.0f, panel.w - 28.0f, 34.0f}, accent);
    }

    void drawGrowthPanel(const Rect b)
    {
        drawPanel(b, "GROWTH", kGrammarAccent);
        char buf[48];
        const int usable = core::usableGeneration(preset(), intValue(core::kGenerations));
        if (usable < intValue(core::kGenerations))
            std::snprintf(buf, sizeof(buf), "%d (this grammar tops out at %d)", intValue(core::kGenerations), usable);
        else
            std::snprintf(buf, sizeof(buf), "%d", intValue(core::kGenerations));
        slider(core::kGenerations, "Generations", buf, b, 0, kGrammarAccent);
        const int grow = intValue(core::kGrowBars);
        if (grow == 0)
            std::snprintf(buf, sizeof(buf), "off (final generation)");
        else
            std::snprintf(buf, sizeof(buf), "+1 every %d bars", grow);
        slider(core::kGrowBars, "Grow", buf, b, 1, kGrammarAccent);
        std::snprintf(buf, sizeof(buf), "%d", intValue(core::kSeed));
        slider(core::kSeed, "Seed (probability choices)", buf, b, 2, kGrammarAccent);
        std::snprintf(buf, sizeof(buf), "Ch %d", intValue(core::kChannel));
        slider(core::kChannel, "MIDI channel", buf, b, 3, kGrammarAccent);
    }

    void drawPitchPanel(const Rect b)
    {
        drawPanel(b, "PITCH", kPitchAccent);
        drawStepper(core::kScale, "Scale", core::scaleName(intValue(core::kScale)), {b.x + 14.0f, b.y + 42.0f, b.w - 28.0f, 40.0f});
        const std::string root = noteName(intValue(core::kRoot));
        char buf[32];
        const bool held = intValue(core::kPitchSource) >= core::kPitchHeld;
        drawSlider(core::kRoot, "Root note", held ? "lowest held note" : root.c_str(), {b.x + 14.0f, b.y + 92.0f, b.w - 28.0f, 34.0f},
                   kPitchAccent);
        std::snprintf(buf, sizeof(buf), "%d degree%s", intValue(core::kStepSize), intValue(core::kStepSize) == 1 ? "" : "s");
        drawSlider(core::kStepSize, "Step size (+ / -)", buf, {b.x + 14.0f, b.y + 134.0f, b.w - 28.0f, 34.0f}, kPitchAccent);
        std::snprintf(buf, sizeof(buf), "+/- %d degrees", intValue(core::kRange));
        drawSlider(core::kRange, "Range (folds back)", buf, {b.x + 14.0f, b.y + 176.0f, b.w - 28.0f, 34.0f}, kPitchAccent);
    }

    void drawMidiPanel(const Rect b)
    {
        drawPanel(b, "MIDI INPUT", kMidiAccent, "held notes as the pitch source, and two CC sets; the CC sets are off until a channel is chosen");
        const float gap = 16.0f;
        const float w = (b.w - 28.0f - 3.0f * gap) / 4.0f;
        const float x0 = b.x + 14.0f, top = b.y + 38.0f;

        label(x0, top, "Pitch source", true, 11.5f);
        drawSegments(core::kPitchSource, kPitchSourceNames, 3, {x0, top + 18.0f, w, 30.0f}, kMidiAccent);

        char buf[48];
        const int input = intValue(core::kInputChannel);
        if (input == 0)
            std::snprintf(buf, sizeof(buf), "all channels");
        else
            std::snprintf(buf, sizeof(buf), "Ch %d", input);
        drawSlider(core::kInputChannel, "Note channel", buf, {x0 + (w + gap), top, w, 34.0f}, kMidiAccent);

        const int cc = intValue(core::kCcChannel);
        if (cc == 0)
            std::snprintf(buf, sizeof(buf), "off");
        else
            std::snprintf(buf, sizeof(buf), "Ch %d", cc);
        drawSlider(core::kCcChannel, "Drift CC channel", buf, {x0 + 2.0f * (w + gap), top, w, 34.0f}, kMidiAccent);

        const int cond = intValue(core::kConductorCh);
        if (cond == 0)
            std::snprintf(buf, sizeof(buf), "off");
        else
            std::snprintf(buf, sizeof(buf), "Ch %d", cond);
        drawSlider(core::kConductorCh, "Conductor CC channel", buf, {x0 + 3.0f * (w + gap), top, w, 34.0f},
                   kMidiAccent);

        // What each CC does, under its channel control.
        const float hint = top + 44.0f;
        label(x0, top + 54.0f, "Latched keeps the chord after release", true, 10.0f);
        label(x0 + (w + gap), hint, "applies to Held and Latched notes;", true, 10.0f);
        label(x0 + (w + gap), hint + 14.0f, "degree 0 = the lowest note", true, 10.0f);
        label(x0 + 2.0f * (w + gap), hint, "CC 1 probability  2 gate", true, 10.0f);
        label(x0 + 2.0f * (w + gap), hint + 14.0f, "CC 3 range  4 generations", true, 10.0f);
        label(x0 + 3.0f * (w + gap), hint, "CC 21 probability  22 velocity  23 seed", true, 10.0f);
        label(x0 + 3.0f * (w + gap), hint + 14.0f, "CC 24 = 127 restarts at the next bar", true, 10.0f);
    }

    void drawTimingPanel(const Rect b)
    {
        drawPanel(b, "TIMING AND DYNAMICS", kTimeAccent);
        char buf[32];
        if (intValue(core::kGrid) == core::kGridBar)
            std::snprintf(buf, sizeof(buf), "1 bar (time signature)");
        else
            std::snprintf(buf, sizeof(buf), "%s note", core::gridName(intValue(core::kGrid)));
        slider(core::kGrid, "Step grid  (T triplet, . dotted)", buf, b, 0, kTimeAccent);
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kGate) * 100.0f)));
        slider(core::kGate, "Gate", buf, b, 1, kTimeAccent);
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value(core::kProbability) * 100.0f)));
        slider(core::kProbability, "Probability", buf, b, 2, kTimeAccent);
        std::snprintf(buf, sizeof(buf), "%d", intValue(core::kVelocity));
        slider(core::kVelocity, "Velocity (branches softer)", buf, b, 3, kTimeAccent);
    }
};

UI* createUI() { return new SproutUI(); }

END_NAMESPACE_DISTRHO
