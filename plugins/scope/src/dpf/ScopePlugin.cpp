#include "DistrhoPlugin.hpp"

#include "scope_engine.hpp"
#include "scope_params.hpp"

#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

namespace core = downspout::scope;

class ScopePlugin : public Plugin {
public:
    ScopePlugin()
        : Plugin(core::kParamCount, 0, 0)
    {
        const auto& table = core::parameterTable();
        for (int i = 0; i < core::kPeakL; ++i)
            params_[i] = table.specs[static_cast<std::size_t>(i)].defaultValue;
        core::activate(engine_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Scope"; }
    const char* getDescription() const override { return "Spectrum analyzer and level meter."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 'c', 'o', 'p'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParamCount)
            return;
        const core::ParameterSpec& spec = core::parameterTable().specs[index];
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
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index >= core::kParamCount)
            return 0.0f;
        if (index == core::kPeakL) return peakL_;
        if (index == core::kPeakR) return peakR_;
        if (index == core::kRmsL) return rmsL_;
        if (index == core::kRmsR) return rmsR_;
        if (index == core::kCorr) return corr_;
        if (index == core::kLufsMomentaryL) return lufsMomentaryL_;
        if (index == core::kLufsMomentaryR) return lufsMomentaryR_;
        if (index == core::kLufsShortL) return lufsShortL_;
        if (index == core::kLufsShortR) return lufsShortR_;
        if (index == core::kLufsIntegratedL) return lufsIntegratedL_;
        if (index == core::kLufsIntegratedR) return lufsIntegratedR_;
        if (index >= core::kWaveL0 && index < core::kWaveR0) return waveL_[index - core::kWaveL0];
        if (index >= core::kWaveR0 && index < core::kSpec0) return waveR_[index - core::kWaveR0];
        if (index >= core::kSpec0 && index < core::kSpec0 + static_cast<uint32_t>(core::kSpecCount))
            return specBands_[index - core::kSpec0];
        return params_[index];
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index < static_cast<uint32_t>(core::kPeakL))
            params_[index] = value;
    }

    void activate() override
    {
        core::reset(engine_);
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        core::activate(engine_, newSampleRate);
    }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        core::Params p;
        const int rangeIndex = std::clamp(static_cast<int>(std::lround(params_[core::kRange])), 0, 2);
        p.rangeDb = core::kRangeDb[rangeIndex];
        p.speed = params_[core::kSpeed];
        p.hold = params_[core::kHold];
        p.mode = params_[core::kMode];

        core::processBlock(engine_, p, inputs[0], inputs[1], outputs[0], outputs[1], frames);

        peakL_ = 20.0f * std::log10(core::getPeakL(engine_) + 1.0e-6f);
        peakR_ = 20.0f * std::log10(core::getPeakR(engine_) + 1.0e-6f);
        rmsL_ = 20.0f * std::log10(core::getRmsL(engine_) + 1.0e-6f);
        rmsR_ = 20.0f * std::log10(core::getRmsR(engine_) + 1.0e-6f);
        corr_ = core::getCorr(engine_);
        lufsMomentaryL_ = core::getMomentaryLufs(engine_);
        lufsMomentaryR_ = core::getMomentaryRufs(engine_);
        lufsShortL_ = core::getShortLufs(engine_);
        lufsShortR_ = core::getShortRufs(engine_);
        lufsIntegratedL_ = core::getIntegratedLufs(engine_);
        lufsIntegratedR_ = core::getIntegratedRufs(engine_);

        if (engine_.fresh) {
            engine_.fresh = false;
            const auto& bands = core::getBands(engine_);
            for (int i = 0; i < core::kSpecCount; ++i) {
                specBands_[i] = bands[static_cast<std::size_t>(i)];
                requestParameterValueChange(core::kSpec0 + static_cast<uint32_t>(i),
                                           specBands_[i]);
            }
        }
        if (engine_.waveformFresh) {
            engine_.waveformFresh = false;
            const auto& waveformL = core::getWaveformL(engine_);
            const auto& waveformR = core::getWaveformR(engine_);
            for (int i = 0; i < core::kWaveformSize; ++i) {
                const int base = (engine_.waveformPos + core::kWaveformSize - 1) % core::kWaveformSize;
                const int idx = (base + i + 1) % core::kWaveformSize;
                waveL_[static_cast<std::size_t>(i)] = waveformL[static_cast<std::size_t>(idx)];
                waveR_[static_cast<std::size_t>(i)] = waveformR[static_cast<std::size_t>(idx)];
                requestParameterValueChange(core::kWaveL0 + static_cast<uint32_t>(i), waveL_[static_cast<std::size_t>(i)]);
                requestParameterValueChange(core::kWaveR0 + static_cast<uint32_t>(i), waveR_[static_cast<std::size_t>(i)]);
            }
        }

        pushIfMoved(core::kPeakL, sentPeakL_, peakL_);
        pushIfMoved(core::kPeakR, sentPeakR_, peakR_);
        pushIfMoved(core::kRmsL, sentRmsL_, rmsL_);
        pushIfMoved(core::kRmsR, sentRmsR_, rmsR_);
        pushIfMoved(core::kCorr, sentCorr_, corr_);
        pushIfMoved(core::kLufsMomentaryL, sentLufsMomentaryL_, lufsMomentaryL_);
        pushIfMoved(core::kLufsMomentaryR, sentLufsMomentaryR_, lufsMomentaryR_);
        pushIfMoved(core::kLufsShortL, sentLufsShortL_, lufsShortL_);
        pushIfMoved(core::kLufsShortR, sentLufsShortR_, lufsShortR_);
        pushIfMoved(core::kLufsIntegratedL, sentLufsIntegratedL_, lufsIntegratedL_);
        pushIfMoved(core::kLufsIntegratedR, sentLufsIntegratedR_, lufsIntegratedR_);
    }

private:
    void pushIfMoved(const uint32_t index, float& cached, const float value)
    {
        if (std::fabs(value - cached) < 0.05f)
            return;
        cached = value;
        requestParameterValueChange(index, value);
    }

    core::Engine engine_ {};
    float params_[core::kControlCount] {};
    float peakL_ = -120.0f, peakR_ = -120.0f, rmsL_ = -120.0f, rmsR_ = -120.0f, corr_ = 0.0f;
    float lufsMomentaryL_ = -120.0f, lufsMomentaryR_ = -120.0f;
    float lufsShortL_ = -120.0f, lufsShortR_ = -120.0f;
    float lufsIntegratedL_ = -120.0f, lufsIntegratedR_ = -120.0f;
    float waveL_[core::kWaveformSize] {};
    float waveR_[core::kWaveformSize] {};
    float specBands_[core::kSpecCount] {};
    float sentPeakL_ = -120.0f, sentPeakR_ = -120.0f, sentRmsL_ = -120.0f, sentRmsR_ = -120.0f,
          sentCorr_ = 0.0f;
    float sentLufsMomentaryL_ = -120.0f, sentLufsMomentaryR_ = -120.0f;
    float sentLufsShortL_ = -120.0f, sentLufsShortR_ = -120.0f;
    float sentLufsIntegratedL_ = -120.0f, sentLufsIntegratedR_ = -120.0f;
};

Plugin* createPlugin() { return new ScopePlugin(); }

END_NAMESPACE_DISTRHO
