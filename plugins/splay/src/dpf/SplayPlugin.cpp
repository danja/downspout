#include "DistrhoPlugin.hpp"

#include "splay_core_types.hpp"
#include "splay_params.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::splay;

struct EnumTable {
    const char* const* names;
    std::size_t count;
};

template <std::size_t N>
constexpr EnumTable table(const std::array<const char*, N>& names)
{
    return {names.data(), N};
}

constexpr EnumTable enumFor(const core::ParamId id)
{
    switch (id) {
    case core::ParamId::invertR:
    case core::ParamId::bypass:
    case core::ParamId::spectralWidth: return table(core::kOffOnNames);
    case core::ParamId::panLaw: return table(core::kPanLawNames);
    case core::ParamId::msSolo: return table(core::kSoloNames);
    default: return {nullptr, 0};
    }
}

}  // namespace

class SplayPlugin : public Plugin {
public:
    SplayPlugin()
        : Plugin(static_cast<std::uint32_t>(core::kParameterCount), 0, 0)
    {
        core::prepare(engine_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Splay"; }

    const char* getDescription() const override
    {
        return "Stereo matrix and image shaper with rotation, pan-law compensation, independent mid/side "
               "trims and solo, frequency-dependent width, and correlation and mono-fit output meters.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 'p', 'l', 'y'); }

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

        const core::ParamSpec& spec = core::kParameterSpecs[index];
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        const EnumTable names = enumFor(static_cast<core::ParamId>(index));
        if (names.count == 0)
            return;
        auto* values = new ParameterEnumerationValue[names.count];
        for (std::size_t i = 0; i < names.count; ++i) {
            values[i].value = static_cast<float>(i);
            values[i].label = names.names[i];
        }
        parameter.enumValues.count = static_cast<uint8_t>(names.count);
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = values;
        parameter.enumValues.deleteLater = true;
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index < core::kInputParameterCount)
            return parameters_.*core::kParameterFields[index];

        switch (static_cast<core::ParamId>(index)) {
        case core::ParamId::outCorrelation: return core::correlation(engine_);
        case core::ParamId::outMonoCompat: return core::monoCompatibility(engine_);
        default: return 0.0f;
        }
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;  // output parameters are read-only
        parameters_.*core::kParameterFields[index] = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // activate() clears the delay line only: the patch lives in parameters_.
    void activate() override { core::reset(engine_); }

    void sampleRateChanged(const double newSampleRate) override
    {
        core::prepare(engine_, newSampleRate);
    }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        core::processBlock(engine_, parameters_, frames, inputs, outputs);
    }

private:
    core::Parameters parameters_;
    core::EngineState engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplayPlugin)
};

Plugin* createPlugin()
{
    return new SplayPlugin();
}

END_NAMESPACE_DISTRHO
