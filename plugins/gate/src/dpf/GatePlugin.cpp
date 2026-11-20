#include "DistrhoPlugin.hpp"

#include "gate_engine.hpp"
#include "gate_params.hpp"

#include <atomic>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::gate;

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
    case core::ParamId::scSource:
    case core::ParamId::bypass: return table(core::kOffOnNames);
    case core::ParamId::threshold:
    case core::ParamId::hysteresis:
    case core::ParamId::range:
    case core::ParamId::attack:
    case core::ParamId::hold:
    case core::ParamId::release:
    case core::ParamId::scHpf:
    case core::ParamId::outputDb:
    case core::ParamId::outLevel:
    case core::ParamId::outReduction:
    case core::ParamId::outOpen: return {nullptr, 0};
    }
    return {nullptr, 0};
}

}  // namespace

// Linked stereo noise gate. Audio inputs 3/4 are an optional sidechain; when External is
// selected and nothing is connected the detector sees silence and the gate stays closed.
// All controls are ordinary automatable parameters, so there is no plugin state; the three
// outputs are measured from the audio thread for the panel.
class GatePlugin : public Plugin {
public:
    GatePlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
        core::activate(engine_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Gate"; }

    const char* getDescription() const override
    {
        return "Linked stereo noise gate with hysteresis, hold, external sidechain, sidechain high-pass "
               "and live detector, reduction and gate-status meters.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('G', 'a', 't', 'e'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (input && index >= 2) {
            port.groupId = 100;
            port.hints  |= kAudioPortIsSidechain;
            port.name   = index == 2 ? "Sidechain L" : "Sidechain R";
            port.symbol = index == 2 ? "sc_l" : "sc_r";
            return;
        }
        if (index < 2)
            port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initPortGroup(const uint32_t groupId, PortGroup& portGroup) override
    {
        if (groupId == 100) {
            portGroup.name = "Sidechain";
            portGroup.symbol = "sidechain";
        }
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount)
            return;

        const core::ParamSpec& spec = core::specs()[index];
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
        switch (static_cast<core::ParamId>(index)) {
        case core::ParamId::outLevel:     return static_cast<float>(core::detectorDb(engine_));
        case core::ParamId::outReduction: return static_cast<float>(core::reductionDb(engine_));
        case core::ParamId::outOpen:      return core::gateIsOpen(engine_) ? 1.0f : 0.0f;
        default:
            if (index < core::kInputParameterCount)
                return core::field(parameters_, index);
            return 0.0f;
        }
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;  // output parameters are read-only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // activate() and sampleRateChanged() clear runtime state only; the patch lives in parameters_.
    void activate() override
    {
        core::activate(engine_, getSampleRate());
        level_.store(-100.0f);
        reduction_.store(0.0f);
        open_.store(0.0f);
    }

    void sampleRateChanged(const double newSampleRate) override { core::activate(engine_, newSampleRate); }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        core::processBlock(engine_, parameters_, frames, inputs, outputs);
        level_.store(static_cast<float>(core::detectorDb(engine_)));
        reduction_.store(static_cast<float>(core::reductionDb(engine_)));
        open_.store(core::gateIsOpen(engine_) ? 1.0f : 0.0f);
    }

private:
    core::Parameters parameters_ {};
    core::Engine engine_ {};
    std::atomic<float> level_ {-100.0f};
    std::atomic<float> reduction_ {0.0f};
    std::atomic<float> open_ {0.0f};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GatePlugin)
};

Plugin* createPlugin()
{
    return new GatePlugin();
}

END_NAMESPACE_DISTRHO
