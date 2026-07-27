#include "DistrhoPlugin.hpp"

#include "m_mix_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamTotalBars = 0,
    kParamDivision,
    kParamSteps,
    kParamOffset,
    kParamFadeBars,
    kParamGranularity,
    kParamMaintain,
    kParamFade,
    kParamCut,
    kParamFadeDurMax,
    kParamBias,
    kParamVelocityFades,
    kParamMute,
    kParameterCount
};

using CoreParameters = downspout::mmix::Parameters;
using CoreEngineState = downspout::mmix::EngineState;
using CoreMidiEvent = downspout::mmix::InputMidiEvent;
using CoreTransport = downspout::mmix::TransportSnapshot;

CoreTransport toCoreTransport(const TimePosition& timePos, const double sampleRate) {
    CoreTransport transport {};
    transport.playing = timePos.playing;
    transport.valid = timePos.bbt.valid;

    if (timePos.bbt.valid) {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1)
            + (timePos.bbt.ticksPerBeat > 0.0 ? (timePos.bbt.tick / timePos.bbt.ticksPerBeat) : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.beatType = timePos.bbt.beatType;
        transport.bpm = timePos.bbt.beatsPerMinute;
        transport.meter = downspout::meterFromTimeSignature(timePos.bbt.beatsPerBar, timePos.bbt.beatType);
    } else if (timePos.playing && sampleRate > 0.0) {
        transport.valid = true;
        transport.beatsPerBar = timePos.bbt.beatsPerBar > 0.0f ? timePos.bbt.beatsPerBar : 4.0;
        transport.beatType = timePos.bbt.beatType > 0.0f ? timePos.bbt.beatType : 4.0;
        transport.bpm = timePos.bbt.beatsPerMinute > 0.0 ? timePos.bbt.beatsPerMinute : 120.0;
        const double absBeats = (static_cast<double>(timePos.frame) * transport.bpm) / (60.0 * sampleRate);
        const double barPos = absBeats / transport.beatsPerBar;
        transport.bar = std::floor(barPos);
        transport.barBeat = (barPos - transport.bar) * transport.beatsPerBar;
        transport.meter = downspout::meterFromTimeSignature(transport.beatsPerBar, transport.beatType);
    }

    return transport;
}

CoreMidiEvent toCoreMidiEvent(const MidiEvent& event) {
    CoreMidiEvent midi {};
    midi.frame = event.frame;
    midi.size = static_cast<std::uint8_t>(std::min<std::size_t>(event.size, downspout::mmix::kMaxMidiMessageData));

    const uint8_t* const source = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    for (std::size_t i = 0; i < midi.size; ++i) {
        midi.data[i] = source[i];
    }

    return midi;
}

MidiEvent toDpfMidiEvent(const downspout::mmix::ScheduledMidiEvent& event) {
    MidiEvent midi {};
    midi.frame = event.frame;
    midi.size = event.size;
    midi.dataExt = nullptr;
    for (std::size_t i = 0; i < event.size && i < MidiEvent::kDataSize; ++i) {
        midi.data[i] = event.data[i];
    }
    return midi;
}

void initMMixParameter(Parameter& parameter,
                       const char* name,
                       const char* symbol,
                       const float min,
                       const float max,
                       const float def,
                       const bool integer = false,
                       const bool boolean = false) {
    parameter.name = name;
    parameter.symbol = symbol;
    parameter.hints = kParameterIsAutomatable;
    if (integer) {
        parameter.hints |= kParameterIsInteger;
    }
    if (boolean) {
        parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
    }
    parameter.ranges.min = min;
    parameter.ranges.max = max;
    parameter.ranges.def = def;
}

}  // namespace

class MMixPlugin : public Plugin
{
public:
    MMixPlugin()
        : Plugin(kParameterCount, 0, 0)
    {
        parameters_ = downspout::mmix::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override
    {
        return "M-Mix";
    }

    const char* getDescription() const override
    {
        return "Transport-aware MIDI gate combining probabilistic and Euclidean mixer behavior.";
    }

    const char* getMaker() const override
    {
        return "danja";
    }

    const char* getHomePage() const override
    {
        return "https://danja.github.io/downspout/";
    }

    const char* getLicense() const override
    {
        return "MIT";
    }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (!input && index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        switch (index) {
        case kParamTotalBars:
            initMMixParameter(parameter, "Total Bars", "total_bars", 1.0f, 4096.0f, parameters_.totalBars, true);
            break;
        case kParamDivision:
            initMMixParameter(parameter, "Division", "division", 1.0f, 512.0f, parameters_.division, true);
            break;
        case kParamSteps:
            initMMixParameter(parameter, "Steps", "steps", 0.0f, 512.0f, parameters_.steps, true);
            break;
        case kParamOffset:
            initMMixParameter(parameter, "Offset", "offset", 0.0f, 511.0f, parameters_.offset, true);
            break;
        case kParamFadeBars:
            initMMixParameter(parameter, "Euclidean Fade", "fade_bars", 0.0f, 4096.0f, parameters_.fadeBars);
            break;
        case kParamGranularity:
            initMMixParameter(parameter, "Granularity", "granularity", 1.0f, 32.0f, parameters_.granularity, true);
            break;
        case kParamMaintain:
            initMMixParameter(parameter, "Maintain", "maintain", 0.0f, 100.0f, parameters_.maintain);
            break;
        case kParamFade:
            initMMixParameter(parameter, "Fade", "fade", 0.0f, 100.0f, parameters_.fade);
            break;
        case kParamCut:
            initMMixParameter(parameter, "Cut", "cut", 0.0f, 100.0f, parameters_.cut);
            break;
        case kParamFadeDurMax:
            initMMixParameter(parameter, "Fade Dur Max", "fade_dur_max", downspout::mmix::kFadeMinFraction, 1.0f, parameters_.fadeDurMax);
            break;
        case kParamBias:
            initMMixParameter(parameter, "Open Bias", "bias", 0.0f, 100.0f, parameters_.bias);
            break;
        case kParamVelocityFades:
            initMMixParameter(parameter, "Velocity Fades", "velocity_fades", 0.0f, 1.0f, parameters_.velocityFades, true, true);
            break;
        case kParamMute:
            initMMixParameter(parameter, "Mute", "mute", 0.0f, 1.0f, parameters_.mute, true, true);
            break;
        default:
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case kParamTotalBars: return parameters_.totalBars;
        case kParamDivision: return parameters_.division;
        case kParamSteps: return parameters_.steps;
        case kParamOffset: return parameters_.offset;
        case kParamFadeBars: return parameters_.fadeBars;
        case kParamGranularity: return parameters_.granularity;
        case kParamMaintain: return parameters_.maintain;
        case kParamFade: return parameters_.fade;
        case kParamCut: return parameters_.cut;
        case kParamFadeDurMax: return parameters_.fadeDurMax;
        case kParamBias: return parameters_.bias;
        case kParamVelocityFades: return parameters_.velocityFades;
        case kParamMute: return parameters_.mute;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index) {
        case kParamTotalBars: parameters_.totalBars = value; break;
        case kParamDivision: parameters_.division = value; break;
        case kParamSteps: parameters_.steps = value; break;
        case kParamOffset: parameters_.offset = value; break;
        case kParamFadeBars: parameters_.fadeBars = value; break;
        case kParamGranularity: parameters_.granularity = value; break;
        case kParamMaintain: parameters_.maintain = value; break;
        case kParamFade: parameters_.fade = value; break;
        case kParamCut: parameters_.cut = value; break;
        case kParamFadeDurMax: parameters_.fadeDurMax = value; break;
        case kParamBias: parameters_.bias = value; break;
        case kParamVelocityFades: parameters_.velocityFades = value; break;
        case kParamMute: parameters_.mute = value; break;
        default: break;
        }
        parameters_ = downspout::mmix::clampParameters(parameters_);
    }

    void activate() override
    {
        downspout::mmix::activate(engine_);
    }

    void deactivate() override
    {
        downspout::mmix::deactivate(engine_);
    }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        std::array<CoreMidiEvent, 512> inputEvents {};
        const uint32_t eventCount = std::min<uint32_t>(midiEventCount, static_cast<uint32_t>(inputEvents.size()));
        for (uint32_t i = 0; i < eventCount; ++i) {
            inputEvents[i] = toCoreMidiEvent(midiEvents[i]);
        }

        const downspout::mmix::BlockResult result =
            downspout::mmix::processBlock(engine_,
                                          parameters_,
                                          toCoreTransport(getTimePosition(), getSampleRate()),
                                          frames,
                                          getSampleRate(),
                                          inputEvents.data(),
                                          eventCount);

        for (int i = 0; i < result.eventCount; ++i) {
            writeMidiEvent(toDpfMidiEvent(result.events[static_cast<std::size_t>(i)]));
        }
    }

private:
    CoreParameters parameters_ {};
    CoreEngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MMixPlugin)
};

Plugin* createPlugin()
{
    return new MMixPlugin();
}

END_NAMESPACE_DISTRHO
