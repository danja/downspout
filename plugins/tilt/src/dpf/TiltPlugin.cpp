#include "DistrhoPlugin.hpp"

#include "tilt_engine.hpp"
#include "tilt_params.hpp"
#include "tilt_spectrum.hpp"

#include <algorithm>
#include <array>
#include <atomic>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::tilt;

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
    case core::ParamId::slope: return table(core::kSlopeNames);
    case core::ParamId::mode: return table(core::kModeNames);
    case core::ParamId::bypass: return table(core::kOffOnNames);
    case core::ParamId::spectrumSource: return table(core::kSpectrumNames);
    default: return {nullptr, 0};
    }
}

}  // namespace

// Linked stereo master-bus tilt. Every control is an ordinary automatable parameter, so DPF
// saves the patch by symbol; the 64 spectrum bins, two meters and latency are read-only outputs
// published from the audio thread. No plugin state is needed.
class TiltPlugin : public Plugin {
public:
    TiltPlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
        core::activate(engine_, getSampleRate());
        core::resetSpectrum(analyser_);
        core::resetMeter(meterState_);
        publishOutputs();
    }

protected:
    const char* getLabel() const override { return "Tilt"; }

    const char* getDescription() const override
    {
        return "Linked stereo master-bus tone control. Gain follows a straight line in dB against "
               "log frequency, pivoting at Pivot Freq; Angle sets the slope (6, 12 or 24 dB/oct). "
               "Choose zero-latency minimum-phase IIR or linear-phase FFT with reported latency, "
               "and watch the live spectrum behind the response curve.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('T', 'l', 't', 's'); }

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
        if (core::isSpectrumBin(index))
            return spectrum_[index - core::kSpectrumFirst].load(std::memory_order_relaxed);
        if (core::isMeter(index))
            return meter_[index - core::kMeterFirst].load(std::memory_order_relaxed);
        if (index == core::kOutLatency)
            return latencyMs_;
        return index < core::kInputParameterCount ? core::field(parameters_, index) : 0.0f;
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;  // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
        reportLatency();
    }

    // Clears audio and analyser memory only; the patch lives in parameters_.
    void activate() override
    {
        core::reset(engine_);
        core::resetSpectrum(analyser_);
        core::resetMeter(meterState_);
        publishOutputs();
    }

    void sampleRateChanged(const double newSampleRate) override { core::activate(engine_, newSampleRate); }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        const float sampleRate = static_cast<float>(getSampleRate());
        const auto source = static_cast<core::SpectrumSource>(static_cast<int>(parameters_.spectrumSource));
        if (source == core::SpectrumSource::Pre)
            core::analyseBlock(analyser_, sampleRate, frames, inputs[0], inputs[1]);
        core::processBlock(engine_, parameters_, frames, inputs, outputs);
        if (source == core::SpectrumSource::Post)
            core::analyseBlock(analyser_, sampleRate, frames, outputs[0], outputs[1]);
        core::updateMeter(meterState_, sampleRate, frames, outputs[0], outputs[1]);
        if (source == core::SpectrumSource::Off && analyser_.primed) {
            core::resetSpectrum(analyser_);
        }
        reportLatency();
        publishOutputs();
    }

private:
    void reportLatency() noexcept
    {
        const auto samples = static_cast<std::uint32_t>(core::latencySamples(engine_, parameters_));
        latencyMs_ = static_cast<float>(1000.0 * static_cast<double>(samples) / getSampleRate());
        if (samples != reportedLatency_) {
            reportedLatency_ = samples;
            setLatency(samples);
        }
    }

    void publishOutputs() noexcept
    {
        for (std::size_t k = 0; k < spectrum_.size(); ++k)
            spectrum_[k].store(analyser_.levelDb[k], std::memory_order_relaxed);
        for (std::size_t c = 0; c < meter_.size(); ++c)
            meter_[c].store(meterState_.peakDb[c], std::memory_order_relaxed);
    }

    core::Parameters parameters_ {};
    core::Engine engine_ {};
    core::SpectrumState analyser_ {};
    core::MeterState meterState_ {};
    std::array<std::atomic<float>, core::kSpectrumBins> spectrum_ {};
    std::array<std::atomic<float>, 2> meter_ {core::kMeterFloorDb, core::kMeterFloorDb};
    std::uint32_t reportedLatency_ = 0xFFFFFFFFu;
    float latencyMs_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TiltPlugin)
};

Plugin* createPlugin()
{
    return new TiltPlugin();
}

END_NAMESPACE_DISTRHO
