#include "DistrhoPlugin.hpp"

#include "worms_engine.hpp"
#include "worms_params.hpp"
#include "worms_serialization.hpp"

#include <algorithm>
#include <array>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

using CoreControls  = downspout::worms::Controls;
using CoreEngine    = downspout::worms::EngineState;
using CoreTransport = downspout::worms::TransportSnapshot;
using CoreMidiEvent = downspout::worms::ScheduledMidiEvent;
using CoreMidiType  = downspout::worms::MidiEventType;

CoreTransport toCoreTransport(const TimePosition& tp)
{
    CoreTransport t;
    t.playing = tp.playing;
    t.valid   = tp.bbt.valid;
    if (tp.bbt.valid) {
        t.bar        = static_cast<double>(tp.bbt.bar - 1);
        t.barBeat    = static_cast<double>(tp.bbt.beat - 1)
                     + (tp.bbt.ticksPerBeat > 0.0 ? tp.bbt.tick / tp.bbt.ticksPerBeat : 0.0);
        t.beatsPerBar = tp.bbt.beatsPerBar;
        t.beatType    = tp.bbt.beatType;
        t.bpm         = tp.bbt.beatsPerMinute;
        t.meter       = downspout::meterFromTimeSignature(tp.bbt.beatsPerBar, tp.bbt.beatType);
    }
    return t;
}

MidiEvent toDpfMidi(const CoreMidiEvent& ev)
{
    MidiEvent m {};
    m.frame   = ev.frame;
    m.size    = 3;
    m.data[0] = static_cast<uint8_t>((ev.type == CoreMidiType::noteOn ? 0x90 : 0x80) | (ev.channel & 0x0f));
    m.data[1] = ev.note;
    m.data[2] = ev.velocity;
    m.dataExt = nullptr;
    return m;
}

CoreControls controlsFromParams(const float* p)
{
    using namespace downspout::worms;
    CoreControls c;
    c.root      = static_cast<int>(std::lround(p[kParamRoot]));
    c.reg       = static_cast<int>(std::lround(p[kParamReg]));
    c.stepSize  = static_cast<int>(std::lround(p[kParamStepSize]));
    c.patLen    = static_cast<int>(std::lround(p[kParamPatLen]));
    c.density   = p[kParamDensity];
    c.velocity  = p[kParamVelocity];
    c.vary      = p[kParamVary];
    c.seed      = p[kParamSeed];
    c.condCh    = static_cast<int>(std::lround(p[kParamCondCh]));
    for (int i = 0; i < 6; ++i)
        c.rule.turn[i] = static_cast<int>(std::lround(p[kParamRule0 + static_cast<std::uint32_t>(i)]));
    c.quantize  = p[kParamQuantize] >= 0.5f;
    c.scale     = static_cast<int>(std::lround(p[kParamScale]));
    c.midiCh    = static_cast<int>(std::lround(p[kParamMidiCh]));
    c.actionRandomize = static_cast<int>(std::lround(p[kParamActionRandomize]));
    c.actionMutate    = static_cast<int>(std::lround(p[kParamActionMutate]));
    return c;
}

}  // namespace

class WormsPlugin : public Plugin
{
public:
    WormsPlugin()
        : Plugin(downspout::worms::kParameterCount, 0, 2)
    {
        using namespace downspout::worms;
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            params_[i] = kParamSpecs[i].defaultValue;
    }

protected:
    const char* getLabel()       const override { return "ToneWorm"; }
    const char* getDescription() const override { return "Paterson's Worm Tonnetz MIDI generator."; }
    const char* getMaker()       const override { return "danja"; }
    const char* getHomePage()    const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense()     const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('T', 'n', 'W', 'm'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (!input && index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& param) override
    {
        using namespace downspout::worms;
        if (index >= kParameterCount) return;
        const auto& s = kParamSpecs[index];
        param.name   = s.name;
        param.symbol = s.symbol;
        param.hints  = kParameterIsAutomatable;
        if (s.integer) param.hints |= kParameterIsInteger;
        param.ranges.min = s.minimum;
        param.ranges.max = s.maximum;
        param.ranges.def = s.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < downspout::worms::kParameterCount ? params_[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        using namespace downspout::worms;
        if (index < kParameterCount)
            params_[index] = downspout::generative::clampParam(value, kParamSpecs[index]);
    }

    void initState(uint32_t index, State& state) override
    {
        switch (index) {
        case 0: state.key = "controls"; state.defaultValue = ""; break;
        case 1: state.key = "pattern";  state.defaultValue = ""; break;
        default: break;
        }
    }

    void setState(const char* key, const char* value) override
    {
        using namespace downspout::worms;
        const std::string text(value);
        if (std::string_view(key) == "controls") {
            if (auto c = deserializeControls(text)) {
                const Controls clamped = clampControls(*c);
                // Sync params array from deserialized controls
                params_[kParamRoot]      = static_cast<float>(clamped.root);
                params_[kParamReg]       = static_cast<float>(clamped.reg);
                params_[kParamStepSize]  = static_cast<float>(clamped.stepSize);
                params_[kParamPatLen]    = static_cast<float>(clamped.patLen);
                params_[kParamDensity]   = clamped.density;
                params_[kParamVelocity]  = clamped.velocity;
                params_[kParamVary]      = clamped.vary;
                params_[kParamSeed]      = clamped.seed;
                params_[kParamCondCh]    = static_cast<float>(clamped.condCh);
                for (int i = 0; i < 6; ++i)
                    params_[kParamRule0 + static_cast<std::uint32_t>(i)] =
                        static_cast<float>(clamped.rule.turn[i]);
                params_[kParamQuantize]  = clamped.quantize ? 1.0f : 0.0f;
                params_[kParamScale]     = static_cast<float>(clamped.scale);
                params_[kParamMidiCh]    = static_cast<float>(clamped.midiCh);
            }
        } else if (std::string_view(key) == "pattern") {
            if (auto p = deserializePattern(text)) {
                engine_.pattern     = *p;
                engine_.patternValid = true;
            }
        }
    }

    String getState(const char* key) const override
    {
        using namespace downspout::worms;
        if (std::string_view(key) == "controls")
            return String(serializeControls(controlsFromParams(params_.data())).c_str());
        if (std::string_view(key) == "pattern")
            return String(serializePattern(engine_.pattern).c_str());
        return {};
    }

    void activate() override
    {
        using namespace downspout::worms;
        BlockResult dummy;
        const Controls c = clampControls(controlsFromParams(params_.data()));
        activate(engine_, c);
    }

    void deactivate() override
    {
        downspout::worms::BlockResult dummy;
        downspout::worms::deactivate(engine_, dummy);
    }

    void run(const float**,
             float** outputs,
             uint32_t frames,
             const MidiEvent* midiEvents,
             uint32_t midiEventCount) override
    {
        using namespace downspout::worms;
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        // Conductor CC extraction
        const int condCh = static_cast<int>(std::lround(params_[kParamCondCh]));
        if (condCh > 0 && midiEvents != nullptr) {
            const int ch = condCh - 1;
            for (uint32_t i = 0; i < midiEventCount; ++i) {
                const auto& ev = midiEvents[i];
                if (ev.size >= 3 && (ev.data[0] & 0xf0) == 0xb0 && (ev.data[0] & 0x0f) == ch) {
                    const uint8_t cc  = ev.data[1];
                    const uint8_t val = ev.data[2];
                    switch (cc) {
                    case 21: params_[kParamDensity]  = std::clamp(val / 127.0f, 0.0f, 1.0f); break;
                    case 22: params_[kParamVelocity] = std::clamp(val / 127.0f, 0.0f, 1.0f); break;
                    case 23: params_[kParamVary]     = std::clamp(val / 127.0f, 0.0f, 1.0f); break;
                    case 24: if (val == 127) params_[kParamActionMutate] += 1.0f; break;
                    default: break;
                    }
                }
            }
        }

        const Controls c = clampControls(controlsFromParams(params_.data()));
        const BlockResult result = processBlock(engine_, c, toCoreTransport(getTimePosition()),
                                                frames, getSampleRate());

        for (int i = 0; i < result.eventCount; ++i)
            writeMidiEvent(toDpfMidi(result.events[i]));
    }

private:
    std::array<float, downspout::worms::kParameterCount> params_ {};
    CoreEngine engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WormsPlugin)
};

Plugin* createPlugin() { return new WormsPlugin(); }

END_NAMESPACE_DISTRHO
