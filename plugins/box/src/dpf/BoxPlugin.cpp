#include "DistrhoPlugin.hpp"

#include "box_core_types.hpp"
#include "box_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::box;

using core::ParamId;

[[nodiscard]] float clampf(const float value, const float lo, const float hi) noexcept
{
    return std::max(lo, std::min(value, hi));
}

}  // namespace

class BoxPlugin : public Plugin
{
public:
    BoxPlugin()
        : Plugin(static_cast<std::uint32_t>(core::kParameterCount), 0, 0)
    {
        for (std::size_t i = 0; i < core::kParameterCount; ++i) {
            if (!core::kParameterSpecs[i].output)
                values_[i] = core::kParameterSpecs[i].defaultValue;
        }
        publishStatus();
    }

protected:
    const char* getLabel() const override { return "Box"; }
    const char* getDescription() const override
    {
        return "Physical model of a wooden instrument structure: plate modes, soundhole and "
               "cavity, driven by the input and picked up from XY positions. CC 1 size, "
               "2 damping, 3 soundhole, 4 drive.";
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

    int64_t getUniqueId() const override { return d_cconst('B', 'o', 'x', 'X'); }

    void initAudioPort(const bool input, const std::uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const std::uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount)
            return;

        const core::ParamSpec& spec = core::kParameterSpecs[index];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        if (spec.output)
            parameter.hints = kParameterIsOutput;
        else
            parameter.hints = kParameterIsAutomatable;
        if (spec.logarithmic)
            parameter.hints |= kParameterIsLogarithmic;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        if (spec.trigger)
            parameter.hints |= kParameterIsTrigger;
    }

    float getParameterValue(const std::uint32_t index) const override
    {
        return index < core::kParameterCount ? values_[index] : 0.0f;
    }

    void setParameterValue(const std::uint32_t index, const float value) override
    {
        if (index >= core::kParameterCount)
            return;

        const core::ParamSpec& spec = core::kParameterSpecs[index];
        if (spec.output)
            return;  // the processor owns the derived readouts

        float next = clampf(value, spec.minimum, spec.maximum);
        if (spec.integer)
            next = std::round(next);
        if (values_[index] == next)
            return;

        values_[index] = next;
        publishStatus();
    }

    void activate() override
    {
        core::activate(engine_);
        publishStatus();
    }

    void sampleRateChanged(const double) override
    {
        // Only runtime filter state is cleared; the patch stays exactly as saved.
        core::activate(engine_);
        publishStatus();
    }

    void run(const float** inputs, float** outputs, const std::uint32_t frames,
             const MidiEvent* midiEvents, const std::uint32_t midiEventCount) override
    {
        bool controllerChanged = false;
        const int channel = static_cast<int>(values_[core::index(ParamId::ccChannel)]);

        for (std::uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& event = midiEvents[i];
            if (event.size < 3)
                continue;
            const std::uint8_t* const bytes = event.size > MidiEvent::kDataSize
                                            ? event.dataExt : event.data;
            if ((bytes[0] & 0xf0u) != 0xb0u)
                continue;
            if (static_cast<int>((bytes[0] & 0x0fu) + 1u) != channel)
                continue;

            const auto target = core::controllerTarget(bytes[1]);
            if (!target.has_value())
                continue;

            const std::uint32_t parameter = core::index(*target);
            const float next = core::controllerToValue(*target, bytes[2]);
            if (values_[parameter] == next)
                continue;
            values_[parameter] = next;
            controllerChanged = true;
        }
        if (controllerChanged)
            publishStatus();

        std::array<const float*, 2> safeInputs { inputs[0], inputs[1] };
        std::array<float*, 2> safeOutputs { outputs[0], outputs[1] };
        core::processBlock(engine_, coreParameters(), frames, getSampleRate(),
                           safeInputs.data(), safeOutputs.data());
    }

private:
    [[nodiscard]] core::Parameters coreParameters() const noexcept
    {
        core::Parameters p;
        p.body = values_[core::index(ParamId::body)];
        p.size = values_[core::index(ParamId::size)];
        p.thickness = values_[core::index(ParamId::thickness)];
        p.stiffness = values_[core::index(ParamId::stiffness)];
        p.density = values_[core::index(ParamId::density)];
        p.damping = values_[core::index(ParamId::damping)];
        p.hole = values_[core::index(ParamId::hole)];
        p.volume = values_[core::index(ParamId::volume)];
        p.depth = values_[core::index(ParamId::depth)];
        p.drive = values_[core::index(ParamId::drive)];
        p.nonlinear = values_[core::index(ParamId::nonlinear)];
        p.excX = values_[core::index(ParamId::excX)];
        p.excY = values_[core::index(ParamId::excY)];
        p.micX = values_[core::index(ParamId::micX)];
        p.micY = values_[core::index(ParamId::micY)];
        p.width = values_[core::index(ParamId::width)];
        p.mix = values_[core::index(ParamId::mix)];
        p.output = values_[core::index(ParamId::output)];
        p.bypass = values_[core::index(ParamId::bypass)];
        p.ccChannel = values_[core::index(ParamId::ccChannel)];
        p.seed = values_[core::index(ParamId::seed)];
        return core::clampParameters(p);
    }

    void writeCore(const core::Parameters& raw) noexcept
    {
        const core::Parameters p = core::clampParameters(raw);
        values_[core::index(ParamId::body)] = p.body;
        values_[core::index(ParamId::size)] = p.size;
        values_[core::index(ParamId::thickness)] = p.thickness;
        values_[core::index(ParamId::stiffness)] = p.stiffness;
        values_[core::index(ParamId::density)] = p.density;
        values_[core::index(ParamId::damping)] = p.damping;
        values_[core::index(ParamId::hole)] = p.hole;
        values_[core::index(ParamId::volume)] = p.volume;
        values_[core::index(ParamId::depth)] = p.depth;
        values_[core::index(ParamId::drive)] = p.drive;
        values_[core::index(ParamId::nonlinear)] = p.nonlinear;
        values_[core::index(ParamId::excX)] = p.excX;
        values_[core::index(ParamId::excY)] = p.excY;
        values_[core::index(ParamId::micX)] = p.micX;
        values_[core::index(ParamId::micY)] = p.micY;
        values_[core::index(ParamId::width)] = p.width;
        values_[core::index(ParamId::mix)] = p.mix;
        values_[core::index(ParamId::output)] = p.output;
        values_[core::index(ParamId::bypass)] = p.bypass;
        values_[core::index(ParamId::ccChannel)] = p.ccChannel;
        values_[core::index(ParamId::seed)] = p.seed;
    }

    void publishStatus() noexcept
    {
        const core::BodyState body = core::analyseBody(coreParameters(), getSampleRate());
        const auto write = [this](const ParamId id, const float value) {
            const std::uint32_t i = core::index(id);
            const core::ParamSpec& spec = core::kParameterSpecs[i];
            values_[i] = clampf(value, spec.minimum, spec.maximum);
        };
        write(ParamId::outBodyHz, body.bodyHz);
        write(ParamId::outAirHz, body.airHz);
        write(ParamId::outDecay, body.decaySec);
        write(ParamId::outQ, body.q);
    }

    std::array<float, core::kParameterCount> values_ {};
    core::EngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoxPlugin)
};

Plugin* createPlugin()
{
    return new BoxPlugin();
}

END_NAMESPACE_DISTRHO
