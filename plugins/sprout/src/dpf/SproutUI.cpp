#include "downspout/dpf/MagnetoKit.hpp"

#include "sprout_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
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

constexpr const char* kPresetNames[core::kPresetCount] = {"Plant", "Koch", "Dragon", "Sierpinski", "Cantor", "Levy", "Tree", "Custom"};

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
    SproutUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        core::prepare();
        setRulesText(core::presetRules(core::kPresetCustom), false);
    }

protected:
    void onAction(int) override {}

    // The plugin's saved grammar arrives here (on opening the editor, or when a project loads).
    void stateChanged(const char* key, const char* value) override
    {
        if (key != nullptr && value != nullptr && std::strcmp(key, "grammar") == 0 && rules_ != value) {
            setRulesText(value, false);
            caret_ = rules_.size();
            repaint();
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1 && ev.press) {
            const float mx = static_cast<float>(ev.pos.getX());
            const float my = static_cast<float>(ev.pos.getY());
            if (field_.contains(mx, my)) {
                if (preset() != core::kPresetCustom) {
                    // Start from the grammar on show, so a built-in can be tweaked.
                    setRulesText(core::presetRules(preset()), true);
                    commit(core::kPreset, static_cast<float>(core::kPresetCustom));
                }
                editing_ = true;
                caret_ = caretAt(mx);
                repaint();
                return true;
            }
            if (editing_) {
                editing_ = false;
                repaint();
            }
        }
        return MagnetoUI::onMouse(ev);
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (!editing_) return false;
        const char c = ev.string[0] != '\0' ? ev.string[0] : (ev.character >= 0x20 && ev.character < 0x7f ? static_cast<char>(ev.character) : 0);
        if (c >= 0x20 && c < 0x7f) insertText(std::string(1, c));
        return true;  // claimed even when ignored, so the host does not turn typing into shortcuts
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!editing_) return false;
        const bool command = (ev.mod & (kModifierControl | kModifierSuper)) != 0;
        // Some VST3 hosts report Backspace as raw key code 1 (see tuney-vst).
        const bool backspace = ev.key == kKeyBackspace || ev.keycode == 1;
        if (!ev.press) return true;
        if (command && (ev.key == 'v' || ev.key == 'V')) {
            std::size_t size = 0;
            const void* data = getClipboard(size);
            if (data != nullptr && size > 0) {
                std::string pasted;
                for (const char c : std::string(static_cast<const char*>(data), size)) {
                    if (c == '\n') pasted += "; ";  // one rule per line becomes one rule per ;
                    else if (c >= 0x20 && c < 0x7f) pasted += c;
                }
                insertText(pasted);
            }
        } else if (command && (ev.key == 'c' || ev.key == 'C')) {
            setClipboard(nullptr, rules_.data(), rules_.size());
        } else if (command && (ev.key == 'x' || ev.key == 'X')) {
            setClipboard(nullptr, rules_.data(), rules_.size());
            setRulesText("", true);
            caret_ = 0;
        } else if (backspace) {
            if (caret_ > 0) {
                std::string next = rules_;
                next.erase(caret_ - 1, 1);
                --caret_;
                setRulesText(next, true);
            }
        } else if (ev.key == kKeyDelete) {
            if (caret_ < rules_.size()) {
                std::string next = rules_;
                next.erase(caret_, 1);
                setRulesText(next, true);
            }
        } else if (ev.key == kKeyLeft) {
            caret_ = command ? 0 : (caret_ > 0 ? caret_ - 1 : 0);
        } else if (ev.key == kKeyRight) {
            caret_ = command ? rules_.size() : std::min(rules_.size(), caret_ + 1);
        } else if (ev.key == kKeyHome) {
            caret_ = 0;
        } else if (ev.key == kKeyEnd) {
            caret_ = rules_.size();
        } else if (ev.key == kKeyEnter || ev.key == kKeyEscape) {
            editing_ = false;
        }
        repaint();
        return true;
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        beginFrame();
        drawHeader("Sprout", "L-system melody generator",
                   "a grammar is rewritten each generation, then the string is played: F note, f rest, + - pitch, [ ] branch.");
        drawGrammarPanel({24.0f, 108.0f, width - 48.0f, 154.0f});
        drawPatternPanel({24.0f, 274.0f, width - 48.0f, 168.0f});

        const float y = 454.0f, h = 224.0f, gap = 16.0f;
        const float w = (width - 48.0f - 2.0f * gap) / 3.0f;
        drawGrowthPanel({24.0f, y, w, h});
        drawPitchPanel({24.0f + w + gap, y, w, h});
        drawTimingPanel({24.0f + 2.0f * (w + gap), y, w, h});
        drawMidiPanel({24.0f, y + h + gap, width - 48.0f, height - (y + h + gap) - 18.0f});
    }

private:
    int preset() const { return intValue(core::kPreset); }

    std::string rules_;                         // the Custom grammar text being edited
    std::unique_ptr<core::CustomGrammar> good_;  // last text that parsed: what the plot and slider use
    std::string error_;                         // why rules_ does not parse; empty when it does
    std::size_t caret_ = 0;
    float scroll_ = 0.0f;
    bool editing_ = false;
    Rect field_ {};

    // Replaces the Custom grammar text. A text that parses becomes the grammar the pattern plot
    // shows and (when `send`) the one the plugin plays; one that does not keeps the last good
    // grammar in force and shows what to fix.
    void setRulesText(const std::string& text, const bool send)
    {
        rules_ = text.substr(0, core::kMaxCustomText);
        caret_ = std::min(caret_, rules_.size());
        auto parsed = core::parseCustom(rules_);
        if (parsed.grammar) {
            error_.clear();
            good_ = std::move(parsed.grammar);
            if (send) setState("grammar", rules_.c_str());
        } else {
            error_ = parsed.error;
        }
        repaint();
    }

    void insertText(const std::string& added)
    {
        std::string next = rules_;
        next.insert(caret_, added);
        const std::size_t room = core::kMaxCustomText;
        if (next.size() > room) next.resize(room);
        caret_ = std::min(caret_ + added.size(), next.size());
        setRulesText(next, true);
    }

    // Width of the first `count` characters of the rules text at the field's font size.
    float textWidth(const std::string& s, const std::size_t count)
    {
        if (count == 0 || s.empty()) return 0.0f;  // NanoVG asserts on an empty string
        fontSize(13.0f);
        Rectangle<float> bounds;
        return textBounds(0.0f, 0.0f, s.substr(0, count).c_str(), nullptr, bounds);
    }

    std::size_t caretAt(const float mouseX)
    {
        const float target = mouseX - (field_.x + 8.0f) + scroll_;
        std::size_t best = 0;
        float bestDistance = std::abs(target);
        for (std::size_t i = 1; i <= rules_.size(); ++i) {
            const float distance = std::abs(target - textWidth(rules_, i));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    void drawRulesField(const bool custom)
    {
        const auto& t = theme();
        const Rect f = field_;
        beginPath();
        fc(t.panel);
        roundedRect(f.x, f.y, f.w, f.h, laf::kRadiusSmall);
        fill();
        beginPath();
        if (custom && editing_) {
            strokeColor(kGrammarAccent.r, kGrammarAccent.g, kGrammarAccent.b, 255);
            strokeWidth(2.0f);
        } else {
            sc(t.border);
            strokeWidth(1.0f);
        }
        roundedRect(f.x, f.y, f.w, f.h, laf::kRadiusSmall);
        stroke();

        const std::string shown = custom ? rules_ : std::string(core::presetRules(preset()));
        const float inner = f.w - 16.0f;
        if (custom && editing_) {
            // Keep the caret in view by scrolling the text sideways.
            const float caretX = textWidth(rules_, caret_);
            if (caretX - scroll_ > inner - 4.0f) scroll_ = caretX - inner + 4.0f;
            if (caretX - scroll_ < 0.0f) scroll_ = caretX;
        } else {
            scroll_ = 0.0f;
        }
        scissor(f.x + 4.0f, f.y, f.w - 8.0f, f.h);
        fc(custom ? t.textPrimary : t.textDim);
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        if (shown.empty() && custom)
            label(f.x + 8.0f, f.y + f.h * 0.5f, "type a starting string, then rules:  F; F -> F+F-F-F+F", true, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        else
            text(f.x + 8.0f - scroll_, f.y + f.h * 0.5f, shown.c_str(), nullptr);
        if (custom && editing_) {
            const float cx = f.x + 8.0f - scroll_ + textWidth(rules_, caret_);
            beginPath();
            fc(t.textPrimary);
            rect(cx, f.y + 5.0f, 1.5f, f.h - 10.0f);
            fill();
        }
        resetScissor();
    }

    void drawGrammarPanel(const Rect b)
    {
        const bool custom = preset() == core::kPresetCustom;
        drawPanel(b, "GRAMMAR", kGrammarAccent, "each symbol is rewritten once per generation");
        drawSegments(core::kPreset, kPresetNames, core::kPresetCount, {b.x + 14.0f, b.y + 44.0f, b.w - 28.0f, 30.0f}, kGrammarAccent);
        label(b.x + 16.0f, b.y + 88.0f, "Rules");
        field_ = {b.x + 66.0f, b.y + 80.0f, b.w - 80.0f, 28.0f};
        drawRulesField(custom);
        label(b.x + 16.0f, b.y + 116.0f, "F, G note    f rest    + - pitch up or down    [ ] branch    other letters only grow", true, 10.5f);
        if (!custom) {
            label(b.x + 16.0f, b.y + 132.0f, "Click the rules to edit a copy of this grammar, or choose Custom to write your own.", true, 10.5f);
        } else if (!error_.empty()) {
            // Not applied: the plugin keeps playing the last grammar that was valid.
            fillColor(214, 96, 84, 255);
            fontSize(10.5f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(b.x + 16.0f, b.y + 132.0f, ("Not applied - " + error_ + " (still playing the last valid grammar)").c_str(), nullptr);
        } else {
            const std::string status = editing_ ? "Applied as you type. Enter or a click elsewhere finishes. Rules are separated by ;  e.g.  F; F -> F[+F]F"
                                                : "Click the rules to edit. Start string first, then rules separated by ;  e.g.  F; F -> F[+F]F";
            label(b.x + 16.0f, b.y + 132.0f, status.c_str(), true, 10.5f);
        }
    }

    void drawPatternPanel(const Rect b)
    {
        const int shown = value(core::kStatusGeneration) > 0.5f ? intValue(core::kStatusGeneration) : intValue(core::kGenerations);
        const core::Sequence& seq = core::sequenceFor(preset(), shown, good_.get());
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
        const int usable = core::usableGeneration(preset(), intValue(core::kGenerations), good_.get());
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
