#include "DistrhoUI.hpp"
#include "tuney_vst_engine.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {
struct Rect { float x, y, w, h; bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; } };
constexpr const char* kStateKeyTuney = "tuney_state";
constexpr const char* kStateKeyUiEvent = "ui_event";

std::string characterText(const DGL_NAMESPACE::Widget::CharacterInputEvent& ev)
{
    if (ev.string[0] != '\0') {
        const unsigned char first = static_cast<unsigned char>(ev.string[0]);
        if (first < 0x20 || first == 0x7f) return {};
        return ev.string;
    }

    const std::uint32_t codepoint = ev.character;
    if (codepoint < 0x20 || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff))
        return {};

    std::string text;
    if (codepoint <= 0x7f) {
        text.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        text.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        text.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        text.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    return text;
}
}

class TuneyVstUI final : public UI {
public:
    TuneyVstUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::size_t i = 0; i < values_.size(); ++i) values_[i] = downspout::tuney_vst::kParameterSpecs[i].defaultValue;
       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override { if (index < values_.size()) values_[index] = value; repaint(); }
    void stateChanged(const char* key, const char* value) override
    {
        if (!key || std::strcmp(key, kStateKeyTuney) != 0) return;
        downspout::tuney_vst::TuneyState next;
        if (downspout::tuney_vst::deserializeState(value ? value : "", next)) state_ = std::move(next);
        repaint();
    }

    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        beginPath(); rect(0, 0, w, h); fillColor(Color(16, 19, 28)); fill();
        beginPath(); roundedRect(22, 20, w - 44, 82, 14); fillColor(Color(31, 37, 54)); fill();
        fontSize(32); fillColor(Color(238, 220, 142)); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(48, 58, "TUNEY VST", nullptr);
        fontSize(14); fillColor(Color(154, 168, 195)); text(48, 84, "TYPE TEXT. HEAR ITS TUNING.", nullptr);

        textRect_ = {22, 122, w - 44, 292};
        beginPath(); roundedRect(textRect_.x, textRect_.y, textRect_.w, textRect_.h, 12); fillColor(Color(24, 29, 42)); fill();
        beginPath(); roundedRect(textRect_.x + 2, textRect_.y + 2, textRect_.w - 4, textRect_.h - 4, 10); strokeColor(Color(68, 81, 111)); strokeWidth(1.5); stroke();
        fontSize(18); fillColor(Color(226, 232, 244)); textAlign(ALIGN_LEFT | ALIGN_TOP);
        const std::string shown = state_.text.empty() ? "Click here, type, or paste text…" : state_.text;
        textBox(textRect_.x + 22, textRect_.y + 22, textRect_.w - 44, shown.c_str(), nullptr);

        const float by = 438;
        play_ = {22, by, 146, 56}; stop_ = {180, by, 146, 56}; clear_ = {338, by, 146, 56};
        audio_ = {496, by, 146, 56}; midi_ = {654, by, 146, 56}; wave_ = {812, by, 206, 56};
        drawButton(play_, "PLAY", Color(77, 174, 132)); drawButton(stop_, "STOP", Color(208, 99, 91));
        drawButton(clear_, "CLEAR", Color(91, 107, 139));
        drawButton(audio_, values_[downspout::tuney_vst::kParamAudioEnabled] > 0.5f ? "AUDIO ON" : "AUDIO OFF", Color(83, 125, 190));
        drawButton(midi_, values_[downspout::tuney_vst::kParamMidiEnabled] > 0.5f ? "MIDI ON" : "MIDI OFF", Color(128, 96, 184));
        static constexpr const char* waves[] = {"SINE", "SQUARE", "TRIANGLE"};
        drawButton(wave_, waves[std::clamp(static_cast<int>(values_[downspout::tuney_vst::kParamWaveform]), 0, 2)], Color(152, 111, 56));

        preset_ = {22, 510, 300, 48};
        static constexpr const char* presets[] = {"DEFAULT", "WHITE NOTES", "JUST 14", "AMBIENT", "MIDI CONTROLLER"};
        drawButton(preset_, presets[presetIndex_], Color(65, 137, 127));
        fontSize(14); fillColor(Color(154, 168, 195)); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(342, 520, "Focused Unicode typing is recorded. Ctrl/Cmd+V pastes text.", nullptr);
        text(342, 546, "Free-time timing • microtonal audio • ordinary MIDI", nullptr);
        drawMeter(28, 594, w - 56, 34);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || !ev.press) return false;
        const float x = ev.pos.getX(), y = ev.pos.getY();
        if (play_.contains(x, y)) { setState(kStateKeyUiEvent, "play"); return true; }
        if (stop_.contains(x, y)) { setState(kStateKeyUiEvent, "stop"); return true; }
        if (clear_.contains(x, y)) { state_.text.clear(); pushState(); setState(kStateKeyUiEvent, "clear"); repaint(); return true; }
        if (audio_.contains(x, y)) { toggle(downspout::tuney_vst::kParamAudioEnabled); return true; }
        if (midi_.contains(x, y)) { toggle(downspout::tuney_vst::kParamMidiEnabled); return true; }
        if (wave_.contains(x, y)) { setParam(downspout::tuney_vst::kParamWaveform, std::fmod(values_[downspout::tuney_vst::kParamWaveform] + 1.0f, 3.0f)); return true; }
        if (preset_.contains(x, y)) { presetIndex_ = (presetIndex_ + 1) % 5; applyPreset(); return true; }
        return textRect_.contains(x, y);
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        const std::string character = characterText(ev);
        if (character.empty()) return false;
        if (character == "\r") return true;
        state_.text += character;
        setState(kStateKeyUiEvent, (std::string("type:") + character).c_str());
        repaint();
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        // Steinberg VST3 uses raw virtual-key code 1 for Backspace. Accept
        // either representation because hosts differ in which field they set.
        if (ev.key == kKeyBackspace || ev.keycode == 1) {
            if (ev.press) eraseLastCharacter();
            return true;
        }
        // Text arrives separately through onCharacterInput, but VST3 hosts still
        // need the key event marked as handled or they may apply DAW shortcuts
        // such as Space for transport. Claim printable typing on press and
        // release without inserting it here, which would duplicate text events.
        if (ev.key >= kKeySpace && ev.key < kKeyDelete &&
            (ev.mod & (kModifierControl | kModifierSuper)) == 0)
            return true;
        if (!ev.press) return false;
        if ((ev.mod & (kModifierControl | kModifierSuper)) != 0 && (ev.key == 'v' || ev.key == 'V')) {
            std::size_t size = 0; const void* data = getClipboard(size);
            if (data && size) { state_.text.append(static_cast<const char*>(data), size); pushState(); repaint(); }
            return true;
        }
        return false;
    }

private:
    void drawButton(const Rect& r, const char* label, Color color)
    {
        beginPath(); roundedRect(r.x, r.y, r.w, r.h, 9); fillColor(color); fill();
        fontSize(15); fillColor(Color(250, 250, 250)); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); text(r.x + r.w / 2, r.y + r.h / 2, label, nullptr);
    }
    void drawMeter(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x, y, w, h, 8); fillColor(Color(25, 31, 45)); fill();
        const float amount = std::min(1.0f, static_cast<float>(downspout::tuney_vst::splitUtf8(state_.text).size()) / 120.0f);
        beginPath(); roundedRect(x + 3, y + 3, (w - 6) * amount, h - 6, 6); fillColor(Color(238, 190, 86)); fill();
    }
    void setParam(std::uint32_t index, float value) { editParameter(index, true); setParameterValue(index, value); editParameter(index, false); values_[index] = value; repaint(); }
    void toggle(std::uint32_t index) { setParam(index, values_[index] > 0.5f ? 0.0f : 1.0f); }
    void pushState() { const std::string text = downspout::tuney_vst::serializeState(state_); setState(kStateKeyTuney, text.c_str()); }
    void eraseLastCharacter()
    {
        auto chars = downspout::tuney_vst::splitUtf8(state_.text);
        if (chars.empty()) return;
        chars.pop_back();
        state_.text.clear();
        for (const auto& character : chars) state_.text += character;
        pushState();
        repaint();
    }
    void applyPreset()
    {
        state_.notes.clear();
        setParam(downspout::tuney_vst::kParamAudioEnabled, 1.0f);
        setParam(downspout::tuney_vst::kParamMidiEnabled, 1.0f);
        setParam(downspout::tuney_vst::kParamGain, 1.0f);
        setParam(downspout::tuney_vst::kParamMinimumNoteMs, 500.0f);
        setParam(downspout::tuney_vst::kParamWaveform, 2.0f);
        setParam(downspout::tuney_vst::kParamJustLimit, 0.0f);
        setParam(downspout::tuney_vst::kParamTimingScale, 3.0f);
        setParam(downspout::tuney_vst::kParamOverlapMs, 20.0f);
        setParam(downspout::tuney_vst::kParamMidiVelocity, 64.0f);
        if (presetIndex_ == 1) state_.notes = "ABCDEFG";
        else if (presetIndex_ == 2) setParam(downspout::tuney_vst::kParamJustLimit, 14.0f);
        else if (presetIndex_ == 3) {
            setParam(downspout::tuney_vst::kParamGain, 0.5f); setParam(downspout::tuney_vst::kParamMinimumNoteMs, 750.0f);
            setParam(downspout::tuney_vst::kParamWaveform, 0.0f); setParam(downspout::tuney_vst::kParamTimingScale, 4.0f);
            setParam(downspout::tuney_vst::kParamOverlapMs, 80.0f);
        } else if (presetIndex_ == 4) {
            setParam(downspout::tuney_vst::kParamAudioEnabled, 0.0f); setParam(downspout::tuney_vst::kParamMidiVelocity, 96.0f);
        }
        pushState(); repaint();
    }

    std::array<float, downspout::tuney_vst::kParamCount> values_ {};
    downspout::tuney_vst::TuneyState state_ {};
    Rect textRect_ {}, play_ {}, stop_ {}, clear_ {}, audio_ {}, midi_ {}, wave_ {}, preset_ {};
    int presetIndex_ = 0;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TuneyVstUI)
};

UI* createUI() { return new TuneyVstUI(); }

END_NAMESPACE_DISTRHO
