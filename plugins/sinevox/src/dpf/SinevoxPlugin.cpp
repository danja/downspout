#include "DistrhoPlugin.hpp"

#include "sinevox_core_types.hpp"
#include "sinevox_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

using downspout::sinevox::EngineState;
using downspout::sinevox::MidiOutBlock;
using downspout::sinevox::Parameters;
using downspout::sinevox::ParamId;
using downspout::sinevox::ParamSpec;
using downspout::sinevox::kAudioInputs;
using downspout::sinevox::kInputParameterCount;
using downspout::sinevox::kMaxFormants;
using downspout::sinevox::kParameterCount;
using downspout::sinevox::kParameterSpecs;
using downspout::sinevox::index;

ParameterEnumerationValue kModeValues[] = {
    {0.0f, "Vocoder"}, {1.0f, "Sine Wave Speech"}, {2.0f, "Both"},
};
ParameterEnumerationValue kMidiOutValues[] = {
    {0.0f, "Off"}, {1.0f, "Formant CCs"}, {2.0f, "Formant Notes"},
};

void setEnumeration(Parameter& parameter, ParameterEnumerationValue* values, const std::uint8_t count)
{
    parameter.enumValues.count = count;
    parameter.enumValues.restrictedMode = true;
    parameter.enumValues.values = values;
    parameter.enumValues.deleteLater = false;
}

}  // namespace

class SinevoxPlugin : public Plugin
{
public:
    // No states: every setting is a parameter, and DPF already saves those.
    SinevoxPlugin()
        : Plugin(kParameterCount, 0, 0)
    {
        for (std::size_t i = 0; i < kParameterCount; ++i)
            values_[i] = kParameterSpecs[i].defaultValue;
        engine_.sampleRate = getSampleRate();
        downspout::sinevox::activate(engine_);
    }

protected:
    const char* getLabel() const override { return "Sinevox"; }
    const char* getDescription() const override
    {
        return "Formant vocoder and sine-wave-speech generator. Speech on inputs 1/2 "
               "is analysed for up to eight formants (centre frequency, Q, amplitude). "
               "Vocoder mode drives a parallel band-pass bank on inputs 3/4; sine wave "
               "speech mode drives a bank of sine oscillators. MIDI CCs and notes "
               "control it, and the tracked formants can be sent out as CCs or notes.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 'n', 'v', 'x'); }

    void initAudioPort(const bool input, const std::uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (input && index < kAudioInputs) {
            // The routing contract is the plugin's main gotcha, so the port names
            // state it. Ports 0/1 are the speech pair, 2/3 the carrier pair.
            port.groupId = index / 2;
            if (index < 2)
                port.name = index == 0 ? "Speech L" : "Speech R";
            else
                port.name = index == 2 ? "Carrier L" : "Carrier R";
            port.symbol = String("in_") + String(static_cast<int>(index + 1));
        } else if (!input) {
            port.name = index == 0 ? "Out L" : "Out R";
            port.symbol = String("out_") + String(static_cast<int>(index + 1));
        }
    }

    void initPortGroup(const std::uint32_t groupId, PortGroup& group) override
    {
        if (groupId == 0)
            group.name = "Speech (analysed)";
        else if (groupId == 1)
            group.name = "Carrier (vocoder)";
    }

    void initParameter(std::uint32_t index, Parameter& parameter) override
    {
        const ParamSpec& spec = kParameterSpecs[index];

        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;

        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;

        if (index == downspout::sinevox::index(ParamId::mode))
            setEnumeration(parameter, kModeValues, 3);
        else if (index == downspout::sinevox::index(ParamId::midiOut))
            setEnumeration(parameter, kMidiOutValues, 3);
    }

    float getParameterValue(std::uint32_t index) const override
    {
        return index < kParameterCount ? values_[index] : 0.0f;
    }

    void setParameterValue(std::uint32_t index, float value) override
    {
        if (index >= kInputParameterCount)
            return;  // status outputs are owned by the processor

        const ParamSpec& spec = kParameterSpecs[index];
        float v = std::max(spec.minimum, std::min(value, spec.maximum));
        if (spec.integer)
            v = std::round(v);
        values_[index] = v;
    }

    // Clears the runtime state only. The patch is the parameter array and is
    // never reset here: hosts call activate() on transport start and on load.
    void activate() override
    {
        engine_.sampleRate = getSampleRate();
        downspout::sinevox::activate(engine_);
        publishStatus();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        engine_.sampleRate = newSampleRate;
        downspout::sinevox::activate(engine_);
    }

    void run(const float** inputs, float** outputs, std::uint32_t frames,
             const MidiEvent* midiEvents, std::uint32_t midiEventCount) override
    {
        Parameters params = core();

        // One primary MIDI input carries both the controllers and the notes.
        // CC overrides write back into the parameter array so the processor and
        // the host see the same value; the panel does not follow them.
        bool touched = false;
        for (std::uint32_t i = 0; i < midiEventCount; ++i)
            touched |= downspout::sinevox::handleMidi(engine_, params, midiEvents[i].data, midiEvents[i].size);
        if (touched)
            writeCore(params);

        downspout::sinevox::processBlock(engine_, params, frames, getSampleRate(), inputs, outputs);

        MidiOutBlock out;
        downspout::sinevox::collectMidiOut(engine_, params, frames, out);
        for (int i = 0; i < out.count; ++i) {
            const auto& ev = out.events[static_cast<std::size_t>(i)];
            MidiEvent midi {};
            midi.frame = std::min(ev.frame, frames > 0 ? frames - 1 : 0u);
            midi.size = 3;
            midi.data[0] = ev.data[0];
            midi.data[1] = ev.data[1];
            midi.data[2] = ev.data[2];
            writeMidiEvent(midi);
        }

        publishStatus();
    }

private:
    [[nodiscard]] Parameters core() const noexcept
    {
        Parameters p;
        p.mode = values_[index(ParamId::mode)];
        p.formants = values_[index(ParamId::formants)];
        p.bypass = values_[index(ParamId::bypass)];
        p.gateDb = values_[index(ParamId::gateDb)];
        p.bwLimit = values_[index(ParamId::bwLimit)];
        p.glideMs = values_[index(ParamId::glideMs)];
        p.attackMs = values_[index(ParamId::attackMs)];
        p.releaseMs = values_[index(ParamId::releaseMs)];
        p.shift = values_[index(ParamId::shift)];
        p.qScale = values_[index(ParamId::qScale)];
        p.tilt = values_[index(ParamId::tilt)];
        p.levelDb = values_[index(ParamId::levelDb)];
        p.dry = values_[index(ParamId::dry)];
        p.carrierNorm = values_[index(ParamId::carrierNorm)];
        p.spread = values_[index(ParamId::spread)];
        p.noteShift = values_[index(ParamId::noteShift)];
        p.midiOut = values_[index(ParamId::midiOut)];
        p.outChannel = values_[index(ParamId::outChannel)];
        p.ccChannel = values_[index(ParamId::ccChannel)];
        p.ccShift = values_[index(ParamId::ccShift)];
        p.ccQ = values_[index(ParamId::ccQ)];
        p.ccFormants = values_[index(ParamId::ccFormants)];
        p.ccTilt = values_[index(ParamId::ccTilt)];
        p.ccMode = values_[index(ParamId::ccMode)];
        return downspout::sinevox::clampParameters(p);
    }

    void writeCore(const Parameters& p) noexcept
    {
        values_[index(ParamId::mode)] = p.mode;
        values_[index(ParamId::formants)] = p.formants;
        values_[index(ParamId::shift)] = p.shift;
        values_[index(ParamId::qScale)] = p.qScale;
        values_[index(ParamId::tilt)] = p.tilt;
    }

    void publishStatus() noexcept
    {
        values_[index(ParamId::outLevelDb)] = engine_.outLevelDb;
        values_[index(ParamId::outActive)] = static_cast<float>(engine_.outActive);
        for (int i = 0; i < kMaxFormants; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            values_[index(ParamId::outFreq1) + idx] = engine_.outFreq[idx];
            values_[index(ParamId::outAmp1) + idx] = engine_.outAmpDb[idx];
            values_[index(ParamId::outQ1) + idx] = engine_.outQ[idx];
        }
    }

    std::array<float, kParameterCount> values_ {};
    EngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SinevoxPlugin)
};

Plugin* createPlugin()
{
    return new SinevoxPlugin();
}

END_NAMESPACE_DISTRHO
