#include "DistrhoPlugin.hpp"

#include "prism_engine.hpp"
#include "prism_params.hpp"

#include <algorithm>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::prism;

}  // namespace

class PrismPlugin : public Plugin {
public:
    PrismPlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
        core::activate(engine_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Prism"; }

    const char* getDescription() const override
    {
        return "Banded spectral delay: the spectrum is split into up to eight log-spaced bands and each "
               "gets its own left and right delay (up to 2 s, or a note division of the host tempo) and "
               "feedback. Per-band solo, low and high cut, and four FFT resolutions.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('P', 'r', 's', 'm'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount)
            return;

        const core::ParamSpec& spec = core::parameterSpecs()[index];
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        if (spec.logarithmic)
            parameter.hints |= kParameterIsLogarithmic;

        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        const char* const* names = nullptr;
        std::size_t count = 0;
        if (index == core::kSize)
        {
            names = core::kFftSizeNames.data();
            count = core::kFftSizeNames.size();
        }
        else if (index == core::kLink)
        {
            names = core::kLinkNames.data();
            count = core::kLinkNames.size();
        }
        else if (index == core::kSyncL || index == core::kSyncR)
        {
            names = core::kSyncNames.data();
            count = core::kSyncNames.size();
        }
        else if (index >= core::kGlobalCount && index < core::kInputParameterCount &&
                 ((index - core::kGlobalCount) % core::kFieldsPerBand == core::kDivL ||
                  (index - core::kGlobalCount) % core::kFieldsPerBand == core::kDivR))
        {
            names = core::kDivisionNames.data();
            count = core::kDivisionNames.size();
        }
        if (count > 0)
        {
            // DPF keeps this pointer and frees it itself (deleteLater).
            auto* values = new ParameterEnumerationValue[count];
            for (std::size_t i = 0; i < count; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = names[i];
            }
            parameter.enumValues.count = static_cast<uint8_t>(count);
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = true;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index < core::kInputParameterCount)
            return parameters_[index];
        if (index == core::kOutLatency)
            return outLatency_;
        return index == core::kOutTempo ? outTempo_ : 0.0f;
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;  // the output is read-only
        parameters_[index] = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // The patch lives in parameters_, which activation never touches; only audio state is cleared.
    void activate() override { core::reset(engine_); }

    void sampleRateChanged(const double newSampleRate) override { core::activate(engine_, newSampleRate); }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        // The host's BBT tempo, when it provides one; otherwise 120 BPM.
        const TimePosition& position = getTimePosition();
        const double bpm = position.bbt.valid ? position.bbt.beatsPerMinute : 0.0;
        core::setTempo(engine_, bpm);
        outTempo_ = static_cast<float>(std::clamp(engine_.bpm, 20.0, 300.0));
        core::processBlock(engine_, parameters_, inputs[0], inputs[1], outputs[0], outputs[1], frames);
        reportLatency();
    }

private:
    void reportLatency()
    {
        const int samples = core::latencySamples(engine_);
        outLatency_ = static_cast<float>(1000.0 * samples / getSampleRate());
        if (static_cast<uint32_t>(samples) != reportedLatency_)
        {
            reportedLatency_ = static_cast<uint32_t>(samples);
            setLatency(reportedLatency_);
        }
    }

    core::Parameters parameters_;
    core::Engine engine_;
    uint32_t reportedLatency_ = 0xFFFFFFFFu;
    float outLatency_ = 0.0f;
    float outTempo_ = core::kDefaultTempo;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PrismPlugin)
};

Plugin* createPlugin()
{
    return new PrismPlugin();
}

END_NAMESPACE_DISTRHO
