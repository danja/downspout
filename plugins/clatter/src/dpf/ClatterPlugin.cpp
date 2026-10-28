#include "DistrhoPlugin.hpp"
#include "clatter_core_types.hpp"
#include "clatter_params.hpp"

#include <cstdint>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

// Parameter order: Sound, Note Map, then Tune/Decay/Tone for each family, then Level and Width.
// The UI mirrors this numbering (ClatterUI.cpp).
constexpr uint32_t kSound = 0;
constexpr uint32_t kNoteMap = 1;
constexpr uint32_t kFirstTrim = 2;
constexpr uint32_t kLevel = kFirstTrim + clatter::kFamilyCount * 3;
constexpr uint32_t kWidth = kLevel + 1;
constexpr uint32_t kParamCount = kWidth + 1;

const char* const kFamilyNames[clatter::kFamilyCount] = {"Membrane", "Metal", "Wood", "Shaker", "Clap"};
const char* const kFamilySymbols[clatter::kFamilyCount] = {"membrane", "metal", "wood", "shaker", "clap"};

}  // namespace

class ClatterPlugin : public Plugin {
public:
    ClatterPlugin() : Plugin(kParamCount, 0, 0)
    {
        engine_.setSampleRate(static_cast<int>(getSampleRate()));
        engine_.setParams(params_);
    }

protected:
    const char* getLabel() const override { return "Clatter"; }
    const char* getDescription() const override { return "General MIDI percussion generator: toms, hand drums, bells, wood, shakers and clap"; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('C', 'l', 'a', 't'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;
        if (index == kSound) {
            parameter.name = "Sound"; parameter.symbol = "sound";
            parameter.ranges.min = 0; parameter.ranges.max = clatter::kSoundCount - 1; parameter.ranges.def = 0;
            parameter.hints |= kParameterIsInteger;
        } else if (index == kNoteMap) {
            parameter.name = "Note Map"; parameter.symbol = "noteMap";
            parameter.ranges.min = 0; parameter.ranges.max = 1; parameter.ranges.def = 1;
            parameter.hints |= kParameterIsInteger;
        } else if (index >= kFirstTrim && index < kLevel) {
            const uint32_t family = (index - kFirstTrim) / 3, kind = (index - kFirstTrim) % 3;
            const std::string name = std::string(kFamilyNames[family]) + (kind == 0 ? " Tune" : kind == 1 ? " Decay" : " Tone");
            const std::string symbol = std::string(kFamilySymbols[family]) + (kind == 0 ? "_tune" : kind == 1 ? "_decay" : "_tone");
            parameter.name = name.c_str();
            parameter.symbol = symbol.c_str();
            if (kind == 0) { parameter.unit = "st"; parameter.ranges.min = -12; parameter.ranges.max = 12; parameter.ranges.def = 0; }
            if (kind == 1) { parameter.ranges.min = 0.25f; parameter.ranges.max = 4; parameter.ranges.def = 1; parameter.hints |= kParameterIsLogarithmic; }
            if (kind == 2) { parameter.ranges.min = 0; parameter.ranges.max = 1; parameter.ranges.def = 0.5f; }
        } else if (index == kLevel) {
            parameter.name = "Level"; parameter.symbol = "level";
            parameter.ranges.min = 0; parameter.ranges.max = 1; parameter.ranges.def = 0.8f;
        } else if (index == kWidth) {
            parameter.name = "Width"; parameter.symbol = "width";
            parameter.ranges.min = 0; parameter.ranges.max = 1; parameter.ranges.def = 0.6f;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index == kSound) return static_cast<float>(params_.sound);
        if (index == kNoteMap) return static_cast<float>(params_.noteMap);
        if (index >= kFirstTrim && index < kLevel) {
            const clatter::FamilyTrim& f = params_.family[(index - kFirstTrim) / 3];
            const uint32_t kind = (index - kFirstTrim) % 3;
            return kind == 0 ? f.tune : kind == 1 ? f.decay : f.tone;
        }
        if (index == kLevel) return params_.level;
        if (index == kWidth) return params_.width;
        return 0.0f;
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index == kSound) params_.sound = static_cast<int>(value);
        else if (index == kNoteMap) params_.noteMap = static_cast<int>(value);
        else if (index >= kFirstTrim && index < kLevel) {
            clatter::FamilyTrim& f = params_.family[(index - kFirstTrim) / 3];
            const uint32_t kind = (index - kFirstTrim) % 3;
            (kind == 0 ? f.tune : kind == 1 ? f.decay : f.tone) = value;
        }
        else if (index == kLevel) params_.level = value;
        else if (index == kWidth) params_.width = value;
        engine_.setParams(params_);
    }

    void activate() override { engine_.setSampleRate(static_cast<int>(getSampleRate())); engine_.setParams(params_); }
    void sampleRateChanged(const double newSampleRate) override { engine_.setSampleRate(static_cast<int>(newSampleRate)); }

    void run(const float**, float** outputs, const uint32_t frames, const MidiEvent* midiEvents, const uint32_t midiEventCount) override
    {
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& e = midiEvents[i];
            if (e.size < 3 || (e.data[0] & 0xF0) != 0x90 || e.data[2] == 0) continue;
            // General MIDI: only the notes in the table play. Selected: any note plays the Sound parameter.
            const int sound = params_.noteMap == static_cast<int>(clatter::NoteMap::GeneralMidi)
                                  ? clatter::ClatterEngine::soundForNote(e.data[1])
                                  : params_.sound;
            if (sound < 0) continue;
            engine_.trigger(sound, static_cast<float>(e.data[2]) / 127.0f);
        }
        float* left = outputs[0];
        float* right = outputs[1];
        for (uint32_t i = 0; i < frames; ++i) { left[i] = 0.0f; right[i] = 0.0f; }
        engine_.render(left, right, static_cast<int>(frames));
    }

private:
    clatter::Params params_;
    clatter::ClatterEngine engine_;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClatterPlugin)
};

Plugin* createPlugin() { return new ClatterPlugin(); }

END_NAMESPACE_DISTRHO
