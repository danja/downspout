#include "downspout/dpf/MagnetoKit.hpp"

#include "sam_params.hpp"
#include "sam_speech.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace core = downspout::sam;

namespace {

constexpr kit::Accent kTextAccent {198, 132, 58};       // amber
constexpr kit::Accent kVoiceAccent {176, 88, 62};       // hot metal
constexpr kit::Accent kKeysAccent {124, 148, 104};      // olive
constexpr kit::Accent kTransportAccent {92, 140, 156};  // cold steel
constexpr kit::Accent kOutputAccent {120, 126, 170};    // slate violet

constexpr int kActionSpeak = 100;
constexpr int kActionClear = 101;
constexpr int kActionPresetBase = 1;

constexpr float kFontSize = 15.0f;
constexpr float kLineHeight = 22.0f;

constexpr const char* kStateKeyText = "text";

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

constexpr std::uint32_t P(const core::ParamId id) { return core::paramIndex(id); }

}  // namespace

class SamUI : public MagnetoUI {
public:
    SamUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        setText(core::kDefaultText, false);
        caret_ = text_.size();
    }

protected:
    void onAction(const int id) override
    {
        if (id == kActionSpeak) {
            setParameterValue(P(core::ParamId::speak), 1.0f);
        } else if (id == kActionClear) {
            setText("", true);
            caret_ = 0;
            editing_ = true;
            repaint();
        } else if (id >= kActionPresetBase && id < kActionPresetBase + static_cast<int>(core::kVoicePresets.size())) {
            const auto& v = core::kVoicePresets[static_cast<std::size_t>(id - kActionPresetBase)];
            commit(P(core::ParamId::speed), static_cast<float>(v.speed));
            commit(P(core::ParamId::pitch), static_cast<float>(v.pitch));
            commit(P(core::ParamId::throat), static_cast<float>(v.throat));
            commit(P(core::ParamId::mouth), static_cast<float>(v.mouth));
        }
    }

    // The saved text arrives here when the editor opens or a project loads.
    void stateChanged(const char* key, const char* value) override
    {
        if (key != nullptr && value != nullptr && std::strcmp(key, kStateKeyText) == 0 && text_ != value) {
            setText(value, false);
            caret_ = std::min(caret_, text_.size());
            repaint();
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1 && ev.press) {
            const float mx = static_cast<float>(ev.pos.getX());
            const float my = static_cast<float>(ev.pos.getY());
            if (field_.contains(mx, my)) {
                editing_ = true;
                caret_ = caretAtPoint(mx - (field_.x + 10.0f), my - (field_.y + 8.0f) + static_cast<float>(firstLine_) * kLineHeight);
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

    bool onScroll(const ScrollEvent& ev) override
    {
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        if (!field_.contains(mx, my))
            return false;
        firstLine_ = std::max(0, firstLine_ + (ev.delta.getY() < 0.0 ? 1 : -1));
        repaint();
        return true;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (!editing_)
            return false;
        const char c = ev.string[0] != '\0' ? ev.string[0] : (ev.character >= 0x20 && ev.character < 0x7f ? static_cast<char>(ev.character) : 0);
        if (c >= 0x20 && c < 0x7f)
            insertText(std::string(1, c));
        return true;  // claimed even when ignored, so the host does not turn typing into shortcuts
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!editing_)
            return false;
        const bool command = (ev.mod & (kModifierControl | kModifierSuper)) != 0;
        // Some VST3 hosts report Backspace as raw key code 1 (see tuney-vst).
        const bool backspace = ev.key == kKeyBackspace || ev.keycode == 1;
        if (!ev.press)
            return true;

        if (command && (ev.key == 'v' || ev.key == 'V')) {
            std::size_t size = 0;
            const void* data = getClipboard(size);
            if (data != nullptr && size > 0)
                insertText(core::sanitizeText(std::string(static_cast<const char*>(data), size)));
        } else if (command && (ev.key == 'c' || ev.key == 'C')) {
            setClipboard(nullptr, text_.data(), text_.size());
        } else if (command && (ev.key == 'x' || ev.key == 'X')) {
            setClipboard(nullptr, text_.data(), text_.size());
            setText("", true);
            caret_ = 0;
        } else if (command && (ev.key == 'a' || ev.key == 'A')) {
            caret_ = text_.size();
        } else if (backspace) {
            if (caret_ > 0) {
                std::string next = text_;
                next.erase(caret_ - 1, 1);
                --caret_;
                setText(next, true);
            }
        } else if (ev.key == kKeyDelete) {
            if (caret_ < text_.size()) {
                std::string next = text_;
                next.erase(caret_, 1);
                setText(next, true);
            }
        } else if (ev.key == kKeyLeft) {
            caret_ = command ? 0 : (caret_ > 0 ? caret_ - 1 : 0);
        } else if (ev.key == kKeyRight) {
            caret_ = command ? text_.size() : std::min(text_.size(), caret_ + 1);
        } else if (ev.key == kKeyHome) {
            caret_ = 0;
        } else if (ev.key == kKeyEnd) {
            caret_ = text_.size();
        } else if (ev.key == kKeyUp || ev.key == kKeyDown) {
            const Caret c = caretPosition();
            caret_ = caretAtPoint(c.x, c.y + (ev.key == kKeyUp ? -kLineHeight : kLineHeight));
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
        const bool keysMode = intValue(P(core::ParamId::mode)) == 0;

        beginFrame();
        drawHeader("Sam", "software automatic mouth",
                   "a 1982 formant speech synthesiser: play it from MIDI, or let the transport read your text aloud.");

        drawTextPanel({24.0f, 108.0f, width - 48.0f, 248.0f}, keysMode);

        const float y = 368.0f, h = 252.0f, gap = 16.0f;
        const float w = (width - 48.0f - 2.0f * gap) / 3.0f;
        drawVoicePanel({24.0f, y, w, h});
        drawKeysPanel({24.0f + w + gap, y, w, h}, keysMode);
        drawTransportPanel({24.0f + 2.0f * (w + gap), y, w, h}, !keysMode);

        drawOutputPanel({24.0f, y + h + gap, width - 48.0f, height - (y + h + gap) - 18.0f});
    }

private:
    struct Piece {
        std::size_t begin = 0;
        std::size_t length = 0;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;      // of the whole chunk, trailing spaces included
        float wordWidth = 0.0f;  // of the word alone
        int token = -1;          // index into the script's tokens, -1 for leading spaces
    };

    struct Caret {
        float x = 0.0f;
        float y = 0.0f;
    };

    std::string text_;
    std::size_t caret_ = 0;
    bool editing_ = false;
    int firstLine_ = 0;
    Rect field_ {};
    std::vector<Piece> pieces_;
    int lineCount_ = 1;

    // ── Text model ─────────────────────────────────────────────────────────

    void setText(const std::string& text, const bool send)
    {
        text_ = core::sanitizeText(text);
        caret_ = std::min(caret_, text_.size());
        if (send)
            setState(kStateKeyText, text_.c_str());
        repaint();
    }

    void insertText(const std::string& added)
    {
        std::string next = text_;
        next.insert(caret_, added);
        if (next.size() > core::kMaxTextBytes)
            next.resize(core::kMaxTextBytes);
        caret_ = std::min(caret_ + added.size(), next.size());
        setText(next, true);
    }

    // Width of `s` at the field's font size. NanoVG asserts on an empty string.
    float measure(const std::string& s)
    {
        if (s.empty())
            return 0.0f;
        fontSize(kFontSize);
        Rectangle<float> bounds;
        return textBounds(0.0f, 0.0f, s.c_str(), nullptr, bounds);
    }

    // Lays the text out in lines, one chunk (a word and the spaces after it) at a time, wrapping
    // between chunks. Token numbers match core::parseScript, so a chunk can be matched to the
    // word the plugin reports as current.
    void layout(const float innerWidth)
    {
        pieces_.clear();
        float x = 0.0f;
        int line = 0;
        int token = 0;
        std::size_t i = 0;

        if (!text_.empty() && text_[0] == ' ') {
            std::size_t j = 0;
            while (j < text_.size() && text_[j] == ' ')
                ++j;
            Piece lead;
            lead.length = j;
            lead.width = lead.wordWidth = measure(text_.substr(0, j));
            pieces_.push_back(lead);
            x = lead.width;
            i = j;
        }
        while (i < text_.size()) {
            std::size_t j = i;
            while (j < text_.size() && text_[j] != ' ')
                ++j;
            const std::size_t wordEnd = j;
            while (j < text_.size() && text_[j] == ' ')
                ++j;
            Piece piece;
            piece.begin = i;
            piece.length = j - i;
            piece.wordWidth = measure(text_.substr(i, wordEnd - i));
            piece.width = measure(text_.substr(i, j - i));
            if (x > 0.0f && x + piece.wordWidth > innerWidth) {
                x = 0.0f;
                ++line;
            }
            piece.x = x;
            piece.y = static_cast<float>(line) * kLineHeight;
            piece.token = token++;
            x += piece.width;
            pieces_.push_back(piece);
            i = j;
        }
        lineCount_ = line + 1;
    }

    // Where the caret is, in field coordinates (before scrolling).
    Caret caretPosition()
    {
        Caret c;
        for (const Piece& p : pieces_) {
            if (p.begin > caret_)
                break;
            c.x = p.x + measure(text_.substr(p.begin, std::min(caret_ - p.begin, p.length)));
            c.y = p.y;
        }
        return c;
    }

    // The caret position nearest a point given in field coordinates (before scrolling).
    std::size_t caretAtPoint(const float x, const float y)
    {
        if (pieces_.empty())
            return 0;
        const float lineY = std::floor(std::max(0.0f, y) / kLineHeight) * kLineHeight;
        if (lineY > pieces_.back().y)
            return text_.size();
        std::size_t best = 0;
        float bestDistance = 1.0e9f;
        for (const Piece& p : pieces_) {
            if (std::fabs(p.y - lineY) > 0.5f)
                continue;
            for (std::size_t k = 0; k <= p.length; ++k) {
                // The end of a chunk belongs to the next one unless it is the last on the line.
                const float px = p.x + measure(text_.substr(p.begin, k));
                const float distance = std::fabs(x - px);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = p.begin + k;
                }
            }
        }
        return std::min(best, text_.size());
    }

    // ── Panels ─────────────────────────────────────────────────────────────

    void drawTextPanel(const Rect b, const bool keysMode)
    {
        const core::Script script = core::parseScript(text_);
        char right[96];
        std::snprintf(right, sizeof(right), "%d word%s  -  _ is a rest", static_cast<int>(script.size()), script.size() == 1 ? "" : "s");
        drawPanel(b, "TEXT", kTextAccent, right);

        // Source and reading controls.
        label(b.x + 14.0f, b.y + 40.0f, "Play it from", true, 11.0f);
        drawSegments(P(core::ParamId::mode), core::kModeNames.data(), 2, {b.x + 14.0f, b.y + 56.0f, 220.0f, 28.0f}, kTextAccent);
        label(b.x + 252.0f, b.y + 40.0f, "Read it as", true, 11.0f);
        static constexpr const char* kReadNames[2] = {"English", "Phonemes"};
        drawSegments(P(core::ParamId::phonetic), kReadNames, 2, {b.x + 252.0f, b.y + 56.0f, 190.0f, 28.0f}, kTextAccent);
        drawButton(kActionClear, "Clear", {b.x + b.w - 96.0f, b.y + 56.0f, 82.0f, 28.0f}, kTextAccent, false);
        const Rect speak {b.x + b.w - 190.0f, b.y + 56.0f, 82.0f, 28.0f};
        drawButton(kActionSpeak, "Speak", speak, kTextAccent, !keysMode);
        if (keysMode) {
            beginPath();
            fc(theme().surface.withAlpha(150));
            roundedRect(speak.x, speak.y, speak.w, speak.h, laf::kRadiusSmall);
            fill();
        }

        // The edit box.
        field_ = {b.x + 14.0f, b.y + 96.0f, b.w - 28.0f, 112.0f};
        drawTextField(script);

        const char* hint = keysMode
            ? "Each note speaks the next word at the pitch you play. Advance chooses how a note picks its word."
            : "One word per step from the Start bar, in time with the host. Speak plays it once without the transport.";
        label(b.x + 14.0f, b.y + 216.0f, hint, true, 11.0f);
        label(b.x + 14.0f, b.y + 231.0f,
              intValue(P(core::ParamId::phonetic)) != 0
                  ? "Phonemes: /HEH3LOW2 WER4LD  - digits 1-8 are stress, / starts a breath, . ? , - punctuate."
                  : "English is spelled out by SAM's own rules. Punctuation shapes the pitch: . falls, ? rises.",
              true, 11.0f);
    }

    void drawTextField(const core::Script& script)
    {
        const auto& t = theme();
        const Rect f = field_;
        beginPath();
        fc(t.panel);
        roundedRect(f.x, f.y, f.w, f.h, laf::kRadiusSmall);
        fill();
        beginPath();
        if (editing_) {
            strokeColor(kTextAccent.r, kTextAccent.g, kTextAccent.b, 255);
            strokeWidth(2.0f);
        } else {
            sc(t.border);
            strokeWidth(1.0f);
        }
        roundedRect(f.x, f.y, f.w, f.h, laf::kRadiusSmall);
        stroke();

        const float inner = f.w - 20.0f;
        layout(inner);
        const int visibleLines = std::max(1, static_cast<int>((f.h - 12.0f) / kLineHeight));
        const int current = static_cast<int>(std::lround(value(P(core::ParamId::outWord)))) - 1;

        // Keep what matters in view: the caret while typing, otherwise the word being spoken.
        float focusY = -1.0f;
        if (editing_) {
            focusY = caretPosition().y;
        } else if (current >= 0) {
            for (const Piece& p : pieces_) {
                if (p.token == current)
                    focusY = p.y;
            }
        }
        if (focusY >= 0.0f) {
            const int focusLine = static_cast<int>(std::lround(focusY / kLineHeight));
            if (focusLine < firstLine_)
                firstLine_ = focusLine;
            if (focusLine >= firstLine_ + visibleLines)
                firstLine_ = focusLine - visibleLines + 1;
        }
        firstLine_ = std::clamp(firstLine_, 0, std::max(0, lineCount_ - visibleLines));
        const float scrollY = static_cast<float>(firstLine_) * kLineHeight;

        scissor(f.x + 4.0f, f.y + 4.0f, f.w - 8.0f, f.h - 8.0f);
        const float ox = f.x + 10.0f;
        const float oy = f.y + 8.0f - scrollY;
        fontSize(kFontSize);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);

        if (text_.empty() && !editing_) {
            label(ox, f.y + f.h * 0.5f, "Click here and type something for Sam to say.", true, kFontSize, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        for (const Piece& p : pieces_) {
            const float py = oy + p.y;
            if (py + kLineHeight < f.y || py > f.y + f.h)
                continue;
            if (p.token == current && p.token >= 0) {
                beginPath();
                fillColor(kTextAccent.r, kTextAccent.g, kTextAccent.b, 255);
                roundedRect(ox + p.x - 3.0f, py + 1.0f, p.wordWidth + 6.0f, kLineHeight - 2.0f, 4.0f);
                fill();
            }
            const bool rest = p.token >= 0 && static_cast<std::size_t>(p.token) < script.size() && script.tokens[static_cast<std::size_t>(p.token)].rest;
            if (p.token == current && p.token >= 0)
                fillColor(250, 248, 242, 255);
            else if (rest)
                fc(t.textDim);
            else
                fc(t.textPrimary);
            fontSize(kFontSize);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(ox + p.x, py + kLineHeight * 0.5f, text_.substr(p.begin, p.length).c_str(), nullptr);
        }
        if (editing_) {
            const Caret c = caretPosition();
            beginPath();
            fc(t.textPrimary);
            rect(ox + c.x, oy + c.y + 2.0f, 1.5f, kLineHeight - 4.0f);
            fill();
        }
        resetScissor();

        if (lineCount_ > visibleLines) {
            // A thin scroll thumb: there is more text than fits.
            const float track = f.h - 16.0f;
            const float thumb = std::max(18.0f, track * static_cast<float>(visibleLines) / static_cast<float>(lineCount_));
            const float room = track - thumb;
            const float at = lineCount_ > visibleLines ? room * static_cast<float>(firstLine_) / static_cast<float>(lineCount_ - visibleLines) : 0.0f;
            beginPath();
            fc(t.border);
            roundedRect(f.x + f.w - 8.0f, f.y + 8.0f + at, 3.0f, thumb, 1.5f);
            fill();
        }
    }

    void slider(const core::ParamId id, const char* name, const char* shown, const Rect panel, const float row, const Accent accent)
    {
        drawSlider(P(id), name, shown, {panel.x + 14.0f, panel.y + 46.0f + row * 42.0f, panel.w - 28.0f, 34.0f}, accent);
    }

    void drawVoicePanel(const Rect b)
    {
        drawPanel(b, "VOICE", kVoiceAccent, "SAM's four controls");
        label(b.x + 14.0f, b.y + 40.0f, "Presets from the SAM manual", true, 11.0f);

        const int speed = intValue(P(core::ParamId::speed)), pitch = intValue(P(core::ParamId::pitch));
        const int throat = intValue(P(core::ParamId::throat)), mouth = intValue(P(core::ParamId::mouth));
        const float gap = 6.0f;
        const float bw = (b.w - 28.0f - 2.0f * gap) / 3.0f;
        for (std::size_t i = 0; i < core::kVoicePresets.size(); ++i) {
            const auto& v = core::kVoicePresets[i];
            const bool active = v.speed == speed && v.pitch == pitch && v.throat == throat && v.mouth == mouth;
            const float col = static_cast<float>(i % 3), row = static_cast<float>(i / 3);
            drawButton(kActionPresetBase + static_cast<int>(i), v.name,
                       {b.x + 14.0f + col * (bw + gap), b.y + 56.0f + row * 30.0f, bw, 26.0f}, kVoiceAccent, active);
        }

        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%d", speed);
        drawSlider(P(core::ParamId::speed), "Speed (lower is faster)", buffer, {b.x + 14.0f, b.y + 126.0f, b.w - 28.0f, 34.0f}, kVoiceAccent);
        std::snprintf(buffer, sizeof(buffer), "%d", mouth);
        drawSlider(P(core::ParamId::mouth), "Mouth (vowel colour)", buffer, {b.x + 14.0f, b.y + 168.0f, b.w - 28.0f, 34.0f}, kVoiceAccent);
        std::snprintf(buffer, sizeof(buffer), "%d", throat);
        drawSlider(P(core::ParamId::throat), "Throat (voice size)", buffer, {b.x + 14.0f, b.y + 210.0f, b.w - 28.0f, 34.0f}, kVoiceAccent);
    }

    // Fades a part of the panel that does nothing in the current mode. The controls underneath stay
    // live; only the look changes. (The kit's setDim() double-blends overlapping fills, which shows
    // as a bright band across a panel header, so a veil is drawn over a normally drawn panel.)
    void veil(const Rect b)
    {
        beginPath();
        fc(theme().background.withAlpha(150));
        roundedRect(b.x, b.y, b.w, b.h, laf::kRadiusPanel);
        fill();
    }

    void drawKeysPanel(const Rect b, const bool active)
    {
        drawPanel(b, "KEYS", kKeysAccent, active ? "MIDI instrument" : "not used in Transport mode");
        drawStepper(P(core::ParamId::midiCh), "MIDI input", core::kMidiChannelNames[static_cast<std::size_t>(std::clamp(intValue(P(core::ParamId::midiCh)), 0, 17))],
                    {b.x + 14.0f, b.y + 42.0f, b.w - 28.0f, 40.0f});

        label(b.x + 14.0f, b.y + 92.0f, "Advance");
        drawSegments(P(core::ParamId::advance), core::kAdvanceNames.data(), 2, {b.x + 14.0f, b.y + 110.0f, b.w - 28.0f, 28.0f}, kKeysAccent);

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%+d st", intValue(P(core::ParamId::transpose)));
        drawSlider(P(core::ParamId::transpose), "Transpose", buffer, {b.x + 14.0f, b.y + 150.0f, b.w - 28.0f, 34.0f}, kKeysAccent);
        std::snprintf(buffer, sizeof(buffer), "%d ms", static_cast<int>(std::lround(value(P(core::ParamId::release)))));
        drawSlider(P(core::ParamId::release), "Release", buffer, {b.x + 14.0f, b.y + 192.0f, b.w - 28.0f, 34.0f}, kKeysAccent);
        if (!active)
            veil(b);
    }

    void drawTransportPanel(const Rect b, const bool active)
    {
        drawPanel(b, "TRANSPORT", kTransportAccent, active ? "text to speech" : "not used in Keys mode");
        const int step = std::clamp(intValue(P(core::ParamId::step)), 0, 7);
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%s%s", core::kStepNames[static_cast<std::size_t>(step)], step < 5 ? " note" : "");
        drawStepper(P(core::ParamId::step), "One word every", buffer, {b.x + 14.0f, b.y + 42.0f, b.w - 28.0f, 40.0f});

        std::snprintf(buffer, sizeof(buffer), "bar %d", intValue(P(core::ParamId::startBar)));
        drawSlider(P(core::ParamId::startBar), "Start at", buffer, {b.x + 14.0f, b.y + 92.0f, b.w - 28.0f, 34.0f}, kTransportAccent);

        const bool mono = intValue(P(core::ParamId::monotone)) != 0;
        const int pitch = intValue(P(core::ParamId::pitch));
        if (mono)
            std::snprintf(buffer, sizeof(buffer), "%d  (about %d Hz)", pitch, static_cast<int>(std::lround(core::kPitchConstant / static_cast<double>(std::max(pitch, 1)))));
        else
            std::snprintf(buffer, sizeof(buffer), "%d", pitch);
        drawSlider(P(core::ParamId::pitch), "Pitch", buffer, {b.x + 14.0f, b.y + 134.0f, b.w - 28.0f, 34.0f}, kTransportAccent);

        const float half = (b.w - 28.0f - 8.0f) * 0.5f;
        label(b.x + 14.0f, b.y + 178.0f, "Loop the text");
        drawSegments(P(core::ParamId::loop), core::kOffOnNames.data(), 2, {b.x + 14.0f, b.y + 196.0f, half, 28.0f}, kTransportAccent);
        label(b.x + 22.0f + half, b.y + 178.0f, "Monotone (sung)");
        drawSegments(P(core::ParamId::monotone), core::kOffOnNames.data(), 2, {b.x + 22.0f + half, b.y + 196.0f, half, 28.0f}, kTransportAccent);
        if (!active)
            veil(b);
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "OUTPUT", kOutputAccent, "mono, to both channels");
        const float half = (b.w - 28.0f - 16.0f) * 0.5f;
        char buffer[32];
        const float tone = value(P(core::ParamId::tone));
        if (tone >= tables().ranges[P(core::ParamId::tone)].maximum - 0.5f)
            std::snprintf(buffer, sizeof(buffer), "open");
        else
            std::snprintf(buffer, sizeof(buffer), "%d Hz", static_cast<int>(std::lround(tone)));
        drawSlider(P(core::ParamId::tone), "Tone (softens SAM's 22 kHz grit)", buffer, {b.x + 14.0f, b.y + 42.0f, half, 34.0f}, kOutputAccent);
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value(P(core::ParamId::level)) * 100.0f)));
        drawSlider(P(core::ParamId::level), "Level", buffer, {b.x + 30.0f + half, b.y + 42.0f, half, 34.0f}, kOutputAccent);
    }
};

UI* createUI() { return new SamUI(); }

END_NAMESPACE_DISTRHO
