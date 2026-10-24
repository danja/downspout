#include "DistrhoPlugin.hpp"

#include "nam_engine.hpp"
#include "nam_params.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::namfx;

constexpr const char* kStateKeyModel = "model_file";
constexpr const char* kStateKeyIr = "ir_file";

}  // namespace

// Neural Amp Modeler player: a .nam model followed by a cabinet impulse response. Both files are
// read on the host's main thread and handed to the audio thread as shared rigs; the audio thread
// only swaps pointers. Superseded rigs are parked in graveyard_ until the audio thread lets go,
// so nothing is ever freed during processing.
class NamPlugin : public Plugin {
public:
    NamPlugin()
        : Plugin(core::kParameterCount, 0, 2)
        , engine_(static_cast<float>(getSampleRate()))
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            values_[i] = core::kParameterSpecs[i].defaultValue;
            pending_[i].store(values_[i]);
            engine_.setParameter(i, values_[i]);
        }
    }

protected:
    const char* getLabel() const override { return "NAM"; }
    const char* getDescription() const override
    {
        return "Neural Amp Modeler player: loads .nam amp models and .wav cabinet impulse responses.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('N', 'A', 'M', 'p'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        const auto& spec = core::kParameterSpecs[index];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.unit = spec.unit;
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        if (spec.logarithmic)
            parameter.hints |= kParameterIsLogarithmic;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    void initState(uint32_t index, State& state) override
    {
        state.key = index == 0 ? kStateKeyModel : kStateKeyIr;
        state.label = index == 0 ? "Model file" : "IR file";
        state.hints = kStateIsFilenamePath;
        state.defaultValue = "";
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case core::kInLevel: return inLevel_.load();
        case core::kOutLevel: return outLevel_.load();
        case core::kStatusModel: return static_cast<float>(statusModel_.load());
        case core::kStatusIr: return static_cast<float>(statusIr_.load());
        case core::kModelRate: return static_cast<float>(modelRate_.load());
        case core::kIrMs: return static_cast<float>(irMs_.load());
        default: return index < core::kParameterCount ? values_[index] : 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= core::kParameterCount || core::kParameterSpecs[index].output)
            return;
        const auto& s = core::kParameterSpecs[index];
        values_[index] = std::clamp(value, s.minimum, s.maximum);
        pending_[index].store(values_[index]);
        dirty_.store(true);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyModel) == 0)
            return String(modelPath_.c_str());
        if (std::strcmp(key, kStateKeyIr) == 0)
            return String(irPath_.c_str());
        return String();
    }

    // Main thread. Loading can take a moment for a large model; audio keeps running on the old
    // rig until the new one is published.
    void setState(const char* key, const char* value) override
    {
        const std::string path = value != nullptr ? value : "";
        if (std::strcmp(key, kStateKeyModel) == 0) {
            modelPath_ = path;
            loadModel();
        } else if (std::strcmp(key, kStateKeyIr) == 0) {
            irPath_ = path;
            loadIr();
        }
    }

    void sampleRateChanged(double newSampleRate) override
    {
        engine_.setSampleRate(static_cast<float>(newSampleRate));
        // Models and IRs are prepared for one rate; rebuild them for the new one.
        loadModel();
        loadIr();
    }

    // Runtime state only; the loaded files and parameters stay.
    void activate() override { engine_.reset(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        if (dirty_.exchange(false)) {
            for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
                if (!core::kParameterSpecs[i].output)
                    engine_.setParameter(i, pending_[i].load());
            }
        }
        if (auto model = std::atomic_load(&modelPublished_); model != modelApplied_) {
            modelApplied_ = std::move(model);
            engine_.setModel(modelApplied_);
        }
        if (auto ir = std::atomic_load(&irPublished_); ir != irApplied_) {
            irApplied_ = std::move(ir);
            engine_.setIr(irApplied_);
        }

        engine_.process(inputs[0], inputs[1], outputs[0], outputs[1], static_cast<int>(frames));

        inLevel_.store(engine_.inputPeakDb());
        outLevel_.store(engine_.outputPeakDb());
    }

private:
    void loadModel()
    {
        std::shared_ptr<core::ModelRig> rig;
        int status = core::kSlotEmpty;
        if (!modelPath_.empty()) {
            std::string error;
            rig = core::ModelRig::load(modelPath_, getSampleRate(), error);
            status = rig ? core::kSlotLoaded : core::kSlotError;
            if (!rig)
                d_stderr2("NAM: could not load model %s: %s", modelPath_.c_str(), error.c_str());
        }
        statusModel_.store(status);
        modelRate_.store(rig ? static_cast<int>(rig->modelRate() + 0.5) : 0);
        setLatency(rig ? static_cast<uint32_t>(rig->latencySamples()) : 0u);
        publish(modelPublished_, std::move(rig));
    }

    void loadIr()
    {
        std::shared_ptr<core::IrRig> rig;
        int status = core::kSlotEmpty;
        if (!irPath_.empty()) {
            std::string error;
            rig = core::IrRig::load(irPath_, getSampleRate(), error);
            status = rig ? core::kSlotLoaded : core::kSlotError;
            if (!rig)
                d_stderr2("NAM: could not load IR %s: %s", irPath_.c_str(), error.c_str());
        }
        statusIr_.store(status);
        irMs_.store(rig ? static_cast<int>(1000.0 * rig->lengthSamples() / getSampleRate() + 0.5) : 0);
        publish(irPublished_, std::move(rig));
    }

    // Swaps in `next`, keeping whatever it replaces alive until the audio thread has dropped it.
    template <class T>
    void publish(std::shared_ptr<T>& slot, std::shared_ptr<T> next)
    {
        graveyard_.erase(std::remove_if(graveyard_.begin(), graveyard_.end(),
                                        [](const std::shared_ptr<void>& p) { return p.use_count() <= 1; }),
                         graveyard_.end());
        if (auto old = std::atomic_load(&slot))
            graveyard_.push_back(std::move(old));
        std::atomic_store(&slot, std::move(next));
    }

    std::array<float, core::kParameterCount> values_ {};
    std::array<std::atomic<float>, core::kParameterCount> pending_ {};
    std::atomic<bool> dirty_ {false};

    core::Engine engine_;
    std::shared_ptr<core::ModelRig> modelPublished_, modelApplied_;
    std::shared_ptr<core::IrRig> irPublished_, irApplied_;
    std::vector<std::shared_ptr<void>> graveyard_;
    std::string modelPath_, irPath_;

    std::atomic<float> inLevel_ {-90.0f}, outLevel_ {-90.0f};
    std::atomic<int> statusModel_ {0}, statusIr_ {0}, modelRate_ {0}, irMs_ {0};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamPlugin)
};

Plugin* createPlugin() { return new NamPlugin(); }

END_NAMESPACE_DISTRHO
