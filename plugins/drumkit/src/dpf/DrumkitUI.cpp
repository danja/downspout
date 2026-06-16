#include "DistrhoUI.hpp"

#include "drumkit_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::drumkit::InstrumentId;
using downspout::drumkit::kInstrumentCount;
using downspout::drumkit::kInstrumentSpecs;
using downspout::drumkit::kParameterCount;
using downspout::drumkit::kParameterSpecs;
using downspout::drumkit::kParamBashDecay;
using downspout::drumkit::kParamBashDrive;
using downspout::drumkit::kParamBashEdge;
using downspout::drumkit::kParamBashNoise;
using downspout::drumkit::kParamBashSize;
using downspout::drumkit::kParamBashSpread;
using downspout::drumkit::kParamBitCrush;
using downspout::drumkit::kParamClapDensity;
using downspout::drumkit::kParamClapMetal;
using downspout::drumkit::kParamClapTone;
using downspout::drumkit::kParamClaveDecay;
using downspout::drumkit::kParamClaveMetal;
using downspout::drumkit::kParamClaveTone;
using downspout::drumkit::kParamClosedHHBrightness;
using downspout::drumkit::kParamClosedHHDecay;
using downspout::drumkit::kParamClosedHHMetal;
using downspout::drumkit::kParamCowbellDecay;
using downspout::drumkit::kParamCowbellMetal;
using downspout::drumkit::kParamCowbellTone;
using downspout::drumkit::kParamCrashBrightness;
using downspout::drumkit::kParamCrashDecay;
using downspout::drumkit::kParamCrashMetal;
using downspout::drumkit::kParamKickDecay;
using downspout::drumkit::kParamKickDrive;
using downspout::drumkit::kParamKickPitch;
using downspout::drumkit::kParamKickPunch;
using downspout::drumkit::kParamKickTransient;
using downspout::drumkit::kParamMasterDrive;
using downspout::drumkit::kParamMasterGain;
using downspout::drumkit::kParamMasterReverb;
using downspout::drumkit::kParamOpenHHBrightness;
using downspout::drumkit::kParamOpenHHDecay;
using downspout::drumkit::kParamOpenHHMetal;
using downspout::drumkit::kParamSnareMetal;
using downspout::drumkit::kParamSnareSnap;
using downspout::drumkit::kParamSnareTone;
using downspout::drumkit::kParamTom1Decay;
using downspout::drumkit::kParamTom1Metal;
using downspout::drumkit::kParamTom1Pitch;
using downspout::drumkit::kParamTom2Decay;
using downspout::drumkit::kParamTom2Metal;
using downspout::drumkit::kParamTom2Pitch;
using downspout::drumkit::normalizedKickPitchToHz;

constexpr std::size_t kMaxVoiceControls = 6;
constexpr std::size_t kMasterControlCount = 4;

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
    int r;
    int g;
    int b;
};

struct ControlDef {
    std::uint32_t parameter;
    const char* label;
};

struct VoiceControls {
    InstrumentId instrument;
    std::array<ControlDef, kMaxVoiceControls> controls;
    std::size_t count;
};

constexpr std::array<Color, kInstrumentCount> kInstrumentColors = {{
    {224, 137, 77},
    {226, 171, 79},
    {215, 96, 93},
    {126, 154, 220},
    {94, 187, 166},
    {140, 176, 91},
    {88, 166, 202},
    {164, 136, 219},
    {206, 111, 151},
    {203, 166, 89},
    {131, 183, 116},
}};

constexpr std::array<VoiceControls, kInstrumentCount> kVoiceControls = {{
    {InstrumentId::Kick, {{{kParamKickPitch, "Pitch"}, {kParamKickDecay, "Decay"}, {kParamKickDrive, "Drive"}, {kParamKickPunch, "Punch"}, {kParamKickTransient, "Transient"}, {0, ""}}}, 5},
    {InstrumentId::Clap, {{{kParamClapDensity, "Density"}, {kParamClapTone, "Tone"}, {kParamClapMetal, "Metal"}, {0, ""}, {0, ""}, {0, ""}}}, 3},
    {InstrumentId::Snare, {{{kParamSnareTone, "Tone"}, {kParamSnareSnap, "Snap"}, {kParamSnareMetal, "Metal"}, {0, ""}, {0, ""}, {0, ""}}}, 3},
    {InstrumentId::Crash, {{{kParamCrashBrightness, "Bright"}, {kParamCrashDecay, "Decay"}, {kParamCrashMetal, "Metal"}, {0, ""}, {0, ""}, {0, ""}}}, 3},
    {InstrumentId::ClosedHH, {{{kParamClosedHHBrightness, "Bright"}, {kParamClosedHHDecay, "Decay"}, {kParamClosedHHMetal, "Metal"}, {0, ""}, {0, ""}, {0, ""}}}, 3},
    {InstrumentId::Tom1, {{{kParamTom1Pitch, "Pitch"}, {kParamTom1Decay, "Decay"}, {kParamTom1Metal, "Metal"}, {0, ""}, {0, ""}, {0, ""}}}, 3},
    {InstrumentId::OpenHH, {{{kParamOpenHHBrightness, "Bright"}, {kParamOpenHHDecay, "Decay"}, {kParamOpenHHMetal, "Metal"}, {0, ""}, {0, ""}, {0, ""}}}, 3},
    {InstrumentId::Tom2, {{{kParamTom2Pitch, "Pitch"}, {kParamTom2Decay, "Decay"}, {kParamTom2Metal, "Metal"}, {0, ""}, {0, ""}, {0, ""}}}, 3},
    {InstrumentId::Bash, {{{kParamBashSize, "Size"}, {kParamBashSpread, "Spread"}, {kParamBashDecay, "Decay"}, {kParamBashDrive, "Drive"}, {kParamBashNoise, "Noise"}, {kParamBashEdge, "Edge"}}}, 6},
    {InstrumentId::Cowbell, {{{kParamCowbellTone, "Tone"}, {kParamCowbellDecay, "Decay"}, {kParamCowbellMetal, "Metal"}, {0, ""}, {0, ""}, {0, ""}}}, 3},
    {InstrumentId::Clave, {{{kParamClaveTone, "Tone"}, {kParamClaveDecay, "Decay"}, {kParamClaveMetal, "Metal"}, {0, ""}, {0, ""}, {0, ""}}}, 3},
}};

constexpr std::array<ControlDef, kMasterControlCount> kMasterControls = {{
    {kParamBitCrush, "Crush"},
    {kParamMasterDrive, "Drive"},
    {kParamMasterReverb, "Reverb"},
    {kParamMasterGain, "Gain"},
}};

[[nodiscard]] float clampf(const float value, const float minimum, const float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] float normalizedValue(const std::uint32_t parameter, const float value)
{
    const auto& spec = kParameterSpecs[parameter];
    if (spec.maximum <= spec.minimum)
        return 0.0f;
    return clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);
}

[[nodiscard]] std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[32];
    const auto& spec = kParameterSpecs[parameter];
    if (spec.boolean)
        std::snprintf(buffer, sizeof(buffer), "%s", value >= 0.5f ? "on" : "off");
    else if (parameter == kParamKickPitch)
        std::snprintf(buffer, sizeof(buffer), "%.0f Hz", normalizedKickPitchToHz(value));
    else if (spec.maximum <= 1.0f)
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    else
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

} // namespace

class DrumkitUI : public UI
{
public:
    DrumkitUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values_[i] = kParameterSpecs[i].defaultValue;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index < values_.size())
        {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 22.0f;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, 72.0f);
        drawInstrumentStrips(pad, 112.0f, width - pad * 2.0f, 258.0f);
        drawVoiceEditor(pad, 404.0f, width * 0.62f - pad * 1.5f, height - 426.0f);
        drawMasterPanel(width * 0.62f + pad * 0.5f, 404.0f, width * 0.38f - pad * 1.5f, height - 426.0f);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press)
        {
            activeControlParam_ = -1;
            activeLevelStrip_ = -1;
            return false;
        }

        for (std::size_t i = 0; i < kInstrumentCount; ++i)
        {
            if (muteRects_[i].contains(x, y))
            {
                const std::uint32_t parameter = kInstrumentSpecs[i].muteParam;
                commitParameter(parameter, values_[parameter] >= 0.5f ? 0.0f : 1.0f);
                return true;
            }

            if (levelRects_[i].contains(x, y))
            {
                selectedInstrument_ = static_cast<int>(i);
                activeLevelStrip_ = selectedInstrument_;
                updateLevelFromPosition(selectedInstrument_, y);
                return true;
            }

            if (stripRects_[i].contains(x, y))
            {
                selectedInstrument_ = static_cast<int>(i);
                repaint();
                return true;
            }
        }

        for (const Rect& rect : controlRects_)
        {
            (void)rect;
        }

        for (std::size_t i = 0; i < selectedControlCount_; ++i)
        {
            if (controlRects_[i].contains(x, y))
            {
                activeControlParam_ = static_cast<int>(selectedControls().controls[i].parameter);
                updateControlFromPosition(activeControlParam_, x, controlRects_[i]);
                return true;
            }
        }

        for (std::size_t i = 0; i < masterRects_.size(); ++i)
        {
            if (masterRects_[i].contains(x, y))
            {
                activeControlParam_ = static_cast<int>(kMasterControls[i].parameter);
                updateControlFromPosition(activeControlParam_, x, masterRects_[i]);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeLevelStrip_ >= 0)
        {
            updateLevelFromPosition(activeLevelStrip_, static_cast<float>(ev.pos.getY()));
            return true;
        }

        if (activeControlParam_ >= 0)
        {
            const std::uint32_t parameter = static_cast<std::uint32_t>(activeControlParam_);
            for (std::size_t i = 0; i < selectedControlCount_; ++i)
            {
                if (selectedControls().controls[i].parameter == parameter)
                {
                    updateControlFromPosition(activeControlParam_, static_cast<float>(ev.pos.getX()), controlRects_[i]);
                    return true;
                }
            }
            for (std::size_t i = 0; i < masterRects_.size(); ++i)
            {
                if (kMasterControls[i].parameter == parameter)
                {
                    updateControlFromPosition(activeControlParam_, static_cast<float>(ev.pos.getX()), masterRects_[i]);
                    return true;
                }
            }
        }

        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        const float direction = ev.delta.getY() > 0.0f ? 1.0f : -1.0f;

        for (std::size_t i = 0; i < kInstrumentCount; ++i)
        {
            if (levelRects_[i].contains(x, y))
            {
                nudgeParameter(kInstrumentSpecs[i].levelParam, direction * 0.03f);
                return true;
            }
        }

        for (std::size_t i = 0; i < selectedControlCount_; ++i)
        {
            if (controlRects_[i].contains(x, y))
            {
                nudgeParameter(selectedControls().controls[i].parameter, direction * 0.02f);
                return true;
            }
        }

        for (std::size_t i = 0; i < masterRects_.size(); ++i)
        {
            if (masterRects_[i].contains(x, y))
            {
                nudgeParameter(kMasterControls[i].parameter, direction * 0.02f);
                return true;
            }
        }

        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kInstrumentCount> stripRects_ {};
    std::array<Rect, kInstrumentCount> muteRects_ {};
    std::array<Rect, kInstrumentCount> levelRects_ {};
    std::array<Rect, kMaxVoiceControls> controlRects_ {};
    std::array<Rect, kMasterControlCount> masterRects_ {};
    int selectedInstrument_ = 0;
    int activeLevelStrip_ = -1;
    int activeControlParam_ = -1;
    std::size_t selectedControlCount_ = 0;

    [[nodiscard]] const VoiceControls& selectedControls() const
    {
        return kVoiceControls[static_cast<std::size_t>(std::max(0, std::min(selectedInstrument_, static_cast<int>(kInstrumentCount - 1))))];
    }

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(13, 15, 17, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(31, 35, 38, 255);
        rect(0.0f, 0.0f, width, 386.0f);
        fill();
        closePath();

        beginPath();
        fillColor(78, 126, 133, 26);
        rect(0.0f, 386.0f, width, height - 386.0f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 7.0f);
        fillColor(18, 22, 25, 238);
        fill();
        closePath();

        fontSize(31.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(238, 241, 239, 255);
        text(x + 22.0f, y + 15.0f, "DrumKit", nullptr);

        fontSize(13.0f);
        fillColor(159, 169, 171, 255);
        text(x + 194.0f, y + 24.0f, "MIDI notes 36, 39, 40, 41, 42, 45, 46, 50, 51, 52, 53", nullptr);

        drawLegend(x + w - 242.0f, y + 19.0f, 214.0f, 34.0f);
    }

    void drawLegend(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 7.0f);
        fillColor(38, 45, 48, 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(209, 218, 216, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, "select strip | drag level | toggle M", nullptr);
    }

    void drawInstrumentStrips(const float x, const float y, const float w, const float h)
    {
        const float gap = 8.0f;
        const float stripW = (w - gap * static_cast<float>(kInstrumentCount - 1)) / static_cast<float>(kInstrumentCount);

        for (std::size_t i = 0; i < kInstrumentCount; ++i)
        {
            const Rect strip {x + static_cast<float>(i) * (stripW + gap), y, stripW, h};
            stripRects_[i] = strip;
            drawStrip(i, strip);
        }
    }

    void drawStrip(const std::size_t index, const Rect& rect)
    {
        const bool selected = static_cast<int>(index) == selectedInstrument_;
        const bool muted = values_[kInstrumentSpecs[index].muteParam] >= 0.5f;
        const Color color = kInstrumentColors[index];

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(selected ? 32 : 20, selected ? 38 : 24, selected ? 41 : 27, 255);
        fill();
        strokeColor(selected ? color.r : 54, selected ? color.g : 60, selected ? color.b : 62, selected ? 220 : 180);
        strokeWidth(selected ? 2.0f : 1.0f);
        stroke();
        closePath();

        beginPath();
        roundedRect(rect.x + 8.0f, rect.y + 8.0f, rect.w - 16.0f, 6.0f, 3.0f);
        fillColor(color.r, color.g, color.b, muted ? 80 : 230);
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(221, 226, 224, muted ? 135 : 255);
        text(rect.x + rect.w * 0.5f, rect.y + 27.0f, kInstrumentSpecs[index].shortName, nullptr);

        char noteText[12];
        std::snprintf(noteText, sizeof(noteText), "%u", static_cast<unsigned>(kInstrumentSpecs[index].midiNote));
        fontSize(10.0f);
        fillColor(132, 143, 145, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 48.0f, noteText, nullptr);

        const Rect meter {rect.x + rect.w * 0.5f - 8.0f, rect.y + 76.0f, 16.0f, 122.0f};
        levelRects_[index] = meter;
        drawLevelFader(index, meter, color, muted);

        const Rect mute {rect.x + 12.0f, rect.y + rect.h - 42.0f, rect.w - 24.0f, 28.0f};
        muteRects_[index] = mute;
        drawMuteButton(mute, color, muted);
    }

    void drawLevelFader(const std::size_t index, const Rect& rect, const Color& color, const bool muted)
    {
        const float norm = normalizedValue(kInstrumentSpecs[index].levelParam, values_[kInstrumentSpecs[index].levelParam]);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 6.0f);
        fillColor(9, 11, 12, 255);
        fill();
        closePath();

        const float fillH = rect.h * norm;
        beginPath();
        roundedRect(rect.x + 3.0f, rect.y + rect.h - fillH + 3.0f, rect.w - 6.0f, std::max(2.0f, fillH - 6.0f), 4.0f);
        fillColor(color.r, color.g, color.b, muted ? 70 : 230);
        fill();
        closePath();

        const float thumbY = rect.y + rect.h - rect.h * norm;
        beginPath();
        roundedRect(rect.x - 10.0f, thumbY - 4.0f, rect.w + 20.0f, 8.0f, 4.0f);
        fillColor(230, 235, 232, muted ? 90 : 230);
        fill();
        closePath();
    }

    void drawMuteButton(const Rect& rect, const Color& color, const bool muted)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 6.0f);
        fillColor(muted ? color.r : 31, muted ? color.g : 36, muted ? color.b : 38, muted ? 225 : 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(muted ? 12 : 190, muted ? 14 : 199, muted ? 15 : 199, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, "M", nullptr);
    }

    void drawVoiceEditor(const float x, const float y, const float w, const float h)
    {
        const auto& voice = selectedControls();
        selectedControlCount_ = voice.count;
        const std::size_t index = static_cast<std::size_t>(selectedInstrument_);
        const Color color = kInstrumentColors[index];

        drawPanel(x, y, w, h);

        fontSize(17.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(232, 237, 235, 255);
        text(x + 18.0f, y + 17.0f, kInstrumentSpecs[index].name, nullptr);

        char noteText[48];
        std::snprintf(noteText, sizeof(noteText), "note %u", static_cast<unsigned>(kInstrumentSpecs[index].midiNote));
        fontSize(12.0f);
        fillColor(color.r, color.g, color.b, 255);
        text(x + 18.0f, y + 42.0f, noteText, nullptr);

        const float startY = y + 82.0f;
        const float rowH = 42.0f;
        const float rowGap = 12.0f;
        for (std::size_t i = 0; i < voice.count; ++i)
        {
            const Rect rect {x + 18.0f, startY + static_cast<float>(i) * (rowH + rowGap), w - 36.0f, rowH};
            controlRects_[i] = rect;
            drawHorizontalControl(voice.controls[i], rect, color);
        }
    }

    void drawMasterPanel(const float x, const float y, const float w, const float h)
    {
        drawPanel(x, y, w, h);

        fontSize(17.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(232, 237, 235, 255);
        text(x + 18.0f, y + 17.0f, "Master Bus", nullptr);

        fontSize(12.0f);
        fillColor(148, 158, 160, 255);
        text(x + 18.0f, y + 42.0f, "fixed pan stage, crush, drive, reverb, output", nullptr);

        const Color color {118, 180, 174};
        const float startY = y + 82.0f;
        const float rowH = 42.0f;
        const float rowGap = 13.0f;
        for (std::size_t i = 0; i < kMasterControls.size(); ++i)
        {
            const Rect rect {x + 18.0f, startY + static_cast<float>(i) * (rowH + rowGap), w - 36.0f, rowH};
            masterRects_[i] = rect;
            drawHorizontalControl(kMasterControls[i], rect, color);
        }
    }

    void drawPanel(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 7.0f);
        fillColor(18, 22, 24, 238);
        fill();
        strokeColor(48, 56, 58, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();
    }

    void drawHorizontalControl(const ControlDef& control, const Rect& rect, const Color& color)
    {
        const float value = values_[control.parameter];
        const float norm = normalizedValue(control.parameter, value);
        const std::string textValue = formatValue(control.parameter, value);

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(210, 217, 215, 255);
        text(rect.x, rect.y - 2.0f, control.label, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(color.r, color.g, color.b, 255);
        text(rect.x + rect.w, rect.y - 2.0f, textValue.c_str(), nullptr);

        const float barY = rect.y + 20.0f;
        beginPath();
        roundedRect(rect.x, barY, rect.w, 12.0f, 6.0f);
        fillColor(37, 43, 45, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x, barY, std::max(8.0f, rect.w * norm), 12.0f, 6.0f);
        fillColor(color.r, color.g, color.b, 220);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + rect.w * norm - 5.0f, barY - 4.0f, 10.0f, 20.0f, 5.0f);
        fillColor(236, 241, 238, 255);
        fill();
        closePath();
    }

    void updateLevelFromPosition(const int stripIndex, const float y)
    {
        if (stripIndex < 0 || stripIndex >= static_cast<int>(kInstrumentCount))
            return;

        const Rect& rect = levelRects_[static_cast<std::size_t>(stripIndex)];
        const float norm = clampf(1.0f - ((y - rect.y) / rect.h), 0.0f, 1.0f);
        const std::uint32_t parameter = kInstrumentSpecs[static_cast<std::size_t>(stripIndex)].levelParam;
        const auto& spec = kParameterSpecs[parameter];
        commitParameter(parameter, spec.minimum + norm * (spec.maximum - spec.minimum));
    }

    void updateControlFromPosition(const int parameterIndex, const float x, const Rect& rect)
    {
        if (parameterIndex < 0 || parameterIndex >= static_cast<int>(kParameterCount))
            return;

        const std::uint32_t parameter = static_cast<std::uint32_t>(parameterIndex);
        const auto& spec = kParameterSpecs[parameter];
        const float norm = clampf((x - rect.x) / rect.w, 0.0f, 1.0f);
        commitParameter(parameter, spec.minimum + norm * (spec.maximum - spec.minimum));
    }

    void nudgeParameter(const std::uint32_t parameter, const float delta)
    {
        const auto& spec = kParameterSpecs[parameter];
        commitParameter(parameter, values_[parameter] + delta * (spec.maximum - spec.minimum));
    }

    void commitParameter(const std::uint32_t parameter, const float value)
    {
        if (parameter >= kParameterCount)
            return;

        const auto& spec = kParameterSpecs[parameter];
        const float clamped = spec.boolean ? (value >= 0.5f ? 1.0f : 0.0f) : clampf(value, spec.minimum, spec.maximum);
        editParameter(parameter, true);
        setParameterValue(parameter, clamped);
        editParameter(parameter, false);
        values_[parameter] = clamped;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumkitUI)
};

UI* createUI()
{
    return new DrumkitUI();
}

END_NAMESPACE_DISTRHO
