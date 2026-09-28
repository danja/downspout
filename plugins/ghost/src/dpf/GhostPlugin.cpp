#include "DistrhoPlugin.hpp"

#include "ghost_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

// Stable parameter indices — never reorder.
enum ParameterIndex : uint32_t {
    kParamSensitivity = 0,
    kParamDensity,
    kParamVelocity,
    kParamDrag,
    kParamMode,
    kParamChannel,
    kParamBaseNote,
    kParamPassInput,
    kParamSeed,
    kParamCCSensitivity,
    kParamCCDensity,
    kParamCCVelocity,
    kParamCCDrag,
    kParamCCChannel,
    kParameterCount
};

constexpr const char* kStateSensitivity   = "sensitivity";
constexpr const char* kStateDensity       = "density";
constexpr const char* kStateVelocity      = "velocity";
constexpr const char* kStateDrag          = "drag";
constexpr const char* kStateMode          = "mode";
constexpr const char* kStateChannel       = "channel";
constexpr const char* kStateBaseNote      = "base_note";
constexpr const char* kStatePassInput     = "pass_input";
constexpr const char* kStateSeed          = "seed";
constexpr const char* kStateCCSensitivity = "cc_sensitivity";
constexpr const char* kStateCCDensity     = "cc_density";
constexpr const char* kStateCCVelocity    = "cc_velocity";
constexpr const char* kStateCCDrag        = "cc_drag";
constexpr const char* kStateCCChannel     = "cc_channel";

constexpr uint32_t kStateCount = 14;

float ccToUnit(uint8_t v) noexcept { return static_cast<float>(v) / 127.0f; }
float ccToVelocity(uint8_t v) noexcept { return std::max(1.0f, std::round(static_cast<float>(v))); }

downspout::generative::Transport toCoreTransport(const TimePosition& x)
{
    downspout::generative::Transport t;
    t.valid = x.bbt.valid;
    t.playing = x.playing;
    if (t.valid) {
        t.bar = x.bbt.bar - 1;
        t.barBeat = x.bbt.beat - 1 + (x.bbt.ticksPerBeat > 0 ? x.bbt.tick / x.bbt.ticksPerBeat : 0);
        t.beatsPerBar = x.bbt.beatsPerBar;
        t.beatType = x.bbt.beatType;
        t.bpm = x.bbt.beatsPerMinute;
    }
    return t;
}

}  // namespace

class GhostPlugin : public Plugin
{
public:
    GhostPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        params_ = downspout::ghost::clampParameters(params_);
    }

protected:
    const char* getLabel()       const override { return "Ghost"; }
    const char* getDescription() const override { return "Audio-triggered ghost-note generator. Listens to audio, quantises ghosts to the transport grid, drums on ch 10 or notes on a selectable channel. Modulated by Drift via CC."; }
    const char* getMaker()       const override { return "danja"; }
    const char* getHomePage()    const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense()     const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('G', 'h', 's', 't'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input "  : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_"     : "out_")    + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = 0;
        switch (index) {
        case kParamSensitivity:   parameter.name = "Sensitivity"; parameter.symbol = "sensitivity"; parameter.ranges = { 0.0f, 1.0f, 0.5f }; break;
        case kParamDensity:       parameter.name = "Density";     parameter.symbol = "density";     parameter.ranges = { 0.0f, 1.0f, 0.35f }; break;
        case kParamVelocity:      parameter.name = "Velocity";    parameter.symbol = "velocity";    parameter.hints = kParameterIsInteger; parameter.ranges = { 1.0f, 127.0f, 90.0f }; break;
        case kParamDrag:          parameter.name = "Drag";        parameter.symbol = "drag";        parameter.ranges = { 0.0f, 1.0f, 0.15f }; break;
        case kParamMode:          parameter.name = "Mode";        parameter.symbol = "mode";        parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 1.0f, 0.0f }; break;
        case kParamChannel:       parameter.name = "Channel";     parameter.symbol = "channel";     parameter.hints = kParameterIsInteger; parameter.ranges = { 1.0f, 16.0f, 10.0f }; break;
        case kParamBaseNote:      parameter.name = "Base Note";   parameter.symbol = "base_note";   parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, 38.0f }; break;
        case kParamPassInput:     parameter.name = "Pass Input";  parameter.symbol = "pass_input";  parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 1.0f, 1.0f }; break;
        case kParamSeed:          parameter.name = "Seed";        parameter.symbol = "seed";        parameter.hints = kParameterIsInteger; parameter.ranges = { 1.0f, 65535.0f, 7.0f }; break;
        case kParamCCSensitivity: parameter.name = "CC Sensitivity"; parameter.symbol = "cc_sensitivity"; parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::ghost::kDefaultCCSensitivity }; break;
        case kParamCCDensity:     parameter.name = "CC Density";  parameter.symbol = "cc_density";  parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::ghost::kDefaultCCDensity }; break;
        case kParamCCVelocity:    parameter.name = "CC Velocity"; parameter.symbol = "cc_velocity"; parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::ghost::kDefaultCCVelocity }; break;
        case kParamCCDrag:        parameter.name = "CC Drag";     parameter.symbol = "cc_drag";     parameter.hints = kParameterIsInteger; parameter.ranges = { 0.0f, 127.0f, downspout::ghost::kDefaultCCDrag }; break;
        case kParamCCChannel:     parameter.name = "CC Channel";  parameter.symbol = "cc_channel";  parameter.hints = kParameterIsInteger; parameter.ranges = { 1.0f, 16.0f, 1.0f }; break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        struct Info { const char* key; const char* label; const char* def; };
        static constexpr Info kInfo[kStateCount] = {
            { kStateSensitivity,   "Sensitivity",   "0.5"  },
            { kStateDensity,       "Density",       "0.35" },
            { kStateVelocity,      "Velocity",      "90"   },
            { kStateDrag,          "Drag",          "0.15" },
            { kStateMode,          "Mode",          "0"    },
            { kStateChannel,       "Channel",       "10"   },
            { kStateBaseNote,      "Base Note",     "38"   },
            { kStatePassInput,     "Pass Input",    "1"    },
            { kStateSeed,          "Seed",          "7"    },
            { kStateCCSensitivity, "CC Sensitivity","1"    },
            { kStateCCDensity,     "CC Density",    "2"    },
            { kStateCCVelocity,    "CC Velocity",   "3"    },
            { kStateCCDrag,        "CC Drag",       "4"    },
            { kStateCCChannel,     "CC Channel",    "1"    },
        };
        if (index < kStateCount) {
            state.key          = kInfo[index].key;
            state.label        = kInfo[index].label;
            state.hints        = kStateIsOnlyForDSP;
            state.defaultValue = kInfo[index].def;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case kParamSensitivity:   return params_.sensitivity;
        case kParamDensity:       return params_.density;
        case kParamVelocity:      return params_.velocity;
        case kParamDrag:          return params_.drag;
        case kParamMode:          return params_.mode;
        case kParamChannel:       return params_.channel;
        case kParamBaseNote:      return params_.baseNote;
        case kParamPassInput:     return params_.passInput;
        case kParamSeed:          return params_.seed;
        case kParamCCSensitivity: return params_.ccSensitivity;
        case kParamCCDensity:     return params_.ccDensity;
        case kParamCCVelocity:    return params_.ccVelocity;
        case kParamCCDrag:        return params_.ccDrag;
        case kParamCCChannel:     return params_.ccChannel;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t, float) override {}

    void setState(const char* key, const char* value) override
    {
        if (!value) return;
        auto f = [&] { return static_cast<float>(std::atof(value)); };
        if      (std::strcmp(key, kStateSensitivity)   == 0) { params_.sensitivity = f(); }
        else if (std::strcmp(key, kStateDensity)       == 0) { params_.density = f(); }
        else if (std::strcmp(key, kStateVelocity)      == 0) { params_.velocity = f(); }
        else if (std::strcmp(key, kStateDrag)          == 0) { params_.drag = f(); }
        else if (std::strcmp(key, kStateMode)          == 0) { params_.mode = f(); }
        else if (std::strcmp(key, kStateChannel)       == 0) { params_.channel = f(); }
        else if (std::strcmp(key, kStateBaseNote)      == 0) { params_.baseNote = f(); }
        else if (std::strcmp(key, kStatePassInput)     == 0) { params_.passInput = f(); }
        else if (std::strcmp(key, kStateSeed)          == 0) { params_.seed = f(); }
        else if (std::strcmp(key, kStateCCSensitivity) == 0) { params_.ccSensitivity = f(); }
        else if (std::strcmp(key, kStateCCDensity)     == 0) { params_.ccDensity = f(); }
        else if (std::strcmp(key, kStateCCVelocity)    == 0) { params_.ccVelocity = f(); }
        else if (std::strcmp(key, kStateCCDrag)        == 0) { params_.ccDrag = f(); }
        else if (std::strcmp(key, kStateCCChannel)     == 0) { params_.ccChannel = f(); }
        params_ = downspout::ghost::clampParameters(params_);
    }

    void activate() override
    {
        downspout::ghost::resetState(engine_);
        ccSensOverride_ = -1.0f;
        ccDensOverride_ = -1.0f;
        ccVelOverride_  = -1.0f;
        ccDragOverride_ = -1.0f;
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const int ccSensNum = static_cast<int>(params_.ccSensitivity);
        const int ccDensNum = static_cast<int>(params_.ccDensity);
        const int ccVelNum  = static_cast<int>(params_.ccVelocity);
        const int ccDragNum = static_cast<int>(params_.ccDrag);
        const int ccCh      = static_cast<int>(params_.ccChannel);

        std::array<downspout::generative::MidiEvent, 512> forwarded {};
        std::uint32_t forwardedCount = 0;

        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const auto& ev = midiEvents[i];
            if (ev.size < 3) continue;
            const uint8_t status = ev.data[0];
            if ((status & 0xF0) == 0xB0 && (status & 0x0F) + 1 == ccCh) {
                const uint8_t evCC  = ev.data[1];
                const uint8_t evVal = ev.data[2];
                if (ccSensNum > 0 && evCC == static_cast<uint8_t>(ccSensNum))
                    ccSensOverride_ = ccToUnit(evVal);
                if (ccDensNum > 0 && evCC == static_cast<uint8_t>(ccDensNum))
                    ccDensOverride_ = ccToUnit(evVal);
                if (ccVelNum  > 0 && evCC == static_cast<uint8_t>(ccVelNum))
                    ccVelOverride_  = ccToVelocity(evVal);
                if (ccDragNum > 0 && evCC == static_cast<uint8_t>(ccDragNum))
                    ccDragOverride_ = ccToUnit(evVal);
                continue;  // routing CCs are consumed, not forwarded
            }
            if (forwardedCount < forwarded.size()) {
                auto& dst = forwarded[forwardedCount++];
                dst.frame = ev.frame;
                dst.size = static_cast<std::uint8_t>(std::min<uint32_t>(ev.size, 4));
                const uint8_t* src = ev.size > MidiEvent::kDataSize ? ev.dataExt : ev.data;
                for (std::uint8_t b = 0; b < dst.size; ++b) dst.data[b] = src[b];
            }
        }

        if (ccSensNum == 0) ccSensOverride_ = -1.0f;
        if (ccDensNum == 0) ccDensOverride_ = -1.0f;
        if (ccVelNum  == 0) ccVelOverride_  = -1.0f;
        if (ccDragNum == 0) ccDragOverride_ = -1.0f;

        const auto block = downspout::ghost::processBlock(
            engine_, params_, toCoreTransport(getTimePosition()), frames, getSampleRate(),
            inputs, outputs, forwarded.data(), forwardedCount,
            ccSensOverride_ >= 0.0f ? ccSensOverride_ : params_.sensitivity,
            ccDensOverride_ >= 0.0f ? ccDensOverride_ : params_.density,
            ccVelOverride_  >= 0.0f ? ccVelOverride_  : params_.velocity,
            ccDragOverride_ >= 0.0f ? ccDragOverride_ : params_.drag);

        for (std::uint32_t i = 0; i < block.count; ++i) {
            MidiEvent e {};
            e.frame = block.events[i].frame;
            e.size = block.events[i].size;
            for (std::uint8_t b = 0; b < e.size; ++b) e.data[b] = block.events[i].data[b];
            writeMidiEvent(e);
        }
    }

private:
    downspout::ghost::Parameters  params_ {};
    downspout::ghost::EngineState engine_ {};
    float ccSensOverride_ = -1.0f;
    float ccDensOverride_ = -1.0f;
    float ccVelOverride_  = -1.0f;
    float ccDragOverride_ = -1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostPlugin)
};

Plugin* createPlugin()
{
    return new GhostPlugin();
}

END_NAMESPACE_DISTRHO
