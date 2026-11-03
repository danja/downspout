#include "DistrhoPlugin.hpp"

#include "chorale_core.hpp"
#include "chorale_params.hpp"

#include <array>
#include <atomic>
#include <vector>

START_NAMESPACE_DISTRHO

namespace core = downspout::chorale;

// A stereo chorus. The controls are ordinary automatable parameters, which DPF saves by symbol, so there is no plugin state. The sweep
// positions and the Rate, Depth, Width and Mix actually in force are output parameters, measured from the audio, that the panel reads.
// CC 1-4 on the CC channel take Rate, Depth, Width and Mix over until the panel or host moves that control again.
class ChoralePlugin : public Plugin
{
public:
    ChoralePlugin() : Plugin(core::kParameterCount, 0, 0)
    {
        core::resetState(engine_);
        for (auto& m : meters_) m.store(0.0f);
        midi_.reserve(1024);
    }

protected:
    const char* getLabel() const override       { return "Chorale"; }
    const char* getDescription() const override { return "A stereo chorus: the three Juno-style voicings I, I+II and II, or a Custom chorus of one to four voices with Rate, Depth and Delay, plus Tone, Width and Mix. CC 1-4 drive Rate, Depth, Width and Mix."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR, DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('C', 'h', 'r', 'l'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        port.groupId = kPortGroupStereo;
        port.name    = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol  = String(input ? "in_" : "out_")       + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount) return;
        const auto& spec = core::specs()[index];
        parameter.name   = spec.name;
        parameter.symbol = spec.symbol;
        parameter.unit   = spec.unit;
        parameter.hints  = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer) parameter.hints |= kParameterIsInteger;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index >= core::kParameterCount) return 0.0f;
        if (core::isOutput(index)) return meters_[index - core::kStatusTap1].load();
        return core::field(parameters_, index);
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || core::isOutput(index)) return;   // outputs are written by the audio thread only
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
        const int target = core::ccTargetOf(index);
        if (target >= 0) core::releaseCc(engine_, static_cast<core::CcTarget>(target));
    }

    // Clears the delay line, filters and LFOs only. The patch lives in the parameters.
    void activate() override
    {
        core::resetState(engine_);
        for (auto& m : meters_) m.store(0.0f);
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        midi_.clear();
        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& e = midiEvents[i];
            if (e.size < 1 || e.size > MidiEvent::kDataSize) continue;
            core::MidiEvent m;
            m.frame = e.frame;
            m.status = e.data[0];
            m.data1 = e.size > 1 ? e.data[1] : 0;
            m.data2 = e.size > 2 ? e.data[2] : 0;
            midi_.push_back(m);
        }
        core::processBlock(engine_, parameters_, static_cast<float>(getSampleRate()), frames, midi_.data(), static_cast<uint32_t>(midi_.size()),
                           inputs[0], inputs[1], outputs[0], outputs[1]);
        for (std::size_t i = 0; i < core::kMaxVoices; ++i) meters_[i].store(engine_.meters.tap[i]);
        meters_[4].store(engine_.meters.rate);
        meters_[5].store(engine_.meters.depth);
        meters_[6].store(engine_.meters.width);
        meters_[7].store(engine_.meters.mix);
    }

private:
    core::Parameters  parameters_ {};
    core::EngineState engine_ {};
    std::vector<core::MidiEvent> midi_;
    std::array<std::atomic<float>, 8> meters_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoralePlugin)
};

Plugin* createPlugin()
{
    return new ChoralePlugin();
}

END_NAMESPACE_DISTRHO
