#include "DistrhoPlugin.hpp"

#include "voxmod_core_types.hpp"
#include "voxmod_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::voxmod::EngineState;
using downspout::voxmod::Parameters;
using downspout::voxmod::ParamId;
using downspout::voxmod::ParamSpec;
using downspout::voxmod::kAudioInputs;
using downspout::voxmod::kAudioOutputs;
using downspout::voxmod::kControllerMap;
using downspout::voxmod::kParameterCount;
using downspout::voxmod::kParameterSpecs;
using downspout::voxmod::kRatioSteps;
using downspout::voxmod::index;

constexpr const char* kStateKeyParameters = "parameters";
constexpr const char* kStateKeyMix = "mix";
constexpr const char* kStateKeyBypass = "bypass";
constexpr const char* kStateKeyRingFreq = "ring_freq";
constexpr const char* kStateKeyRingRatio = "ring_ratio";
constexpr const char* kStateKeyRingShape = "ring_shape";
constexpr const char* kStateKeyRingDepth = "ring_depth";
constexpr const char* kStateKeyBandCount = "band_count";
constexpr const char* kStateKeyBandSpread = "band_spread";
constexpr const char* kStateKeyAttackMs = "attack_ms";
constexpr const char* kStateKeyReleaseMs = "release_ms";
constexpr const char* kStateKeyFormantShift = "formant_shift";
constexpr const char* kStateKeyTilt = "tilt";
constexpr const char* kStateKeyCarrierSource = "carrier_source";
constexpr const char* kStateKeySync = "sync";
constexpr const char* kStateKeyStereoWidth = "stereo_width";
constexpr const char* kStateKeyDrift = "drift";
constexpr const char* kStateKeySeed = "seed";
constexpr const char* kStateKeyCCs = "cc_map";

constexpr std::uint32_t kStateCount = 19;

[[nodiscard]] float clampValue(const float value, const float lo, const float hi) noexcept
{
    return std::max(lo, std::min(value, hi));
}

// Spread a 0-127 controller value across a parameter's full range.
[[nodiscard]] float controllerToParameter(const std::uint32_t parameter, const std::uint8_t value) noexcept
{
    const ParamSpec& spec = kParameterSpecs[parameter];
    return spec.minimum + (static_cast<float>(value) / 127.0f) * (spec.maximum - spec.minimum);
}

}  // namespace

class VoxmodPlugin : public Plugin
{
public:
    VoxmodPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        for (std::size_t i = 0; i < kParameterCount; ++i) {
            if (!kParameterSpecs[i].output)
                values_[i] = kParameterSpecs[i].defaultValue;
        }
        applyToCore();
    }

protected:
    const char* getLabel() const override { return "Voxmod"; }
    const char* getDescription() const override
    {
        return "Combined vocoder and ring modulator. The carrier on inputs 1/2 is "
               "analysed and its spectral envelope is re-imposed on the modulator "
               "from inputs 3/4, while the same modulator is multiplied by an "
               "independent oscillator. Mix blends the two engines; five CCs drive "
               "them.";
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

    int64_t getUniqueId() const override { return d_cconst('V', 'x', 'm', 'd'); }

    void initAudioPort(const bool input, const std::uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (input && index < kAudioInputs) {
            // The routing contract is the plugin's main gotcha, so the port names
            // and groups have to state it rather than leaving it to the manual.
            // Ports 0/1 are the carrier pair, 2/3 the modulator pair.
            port.groupId = index / 2;
            if (index < 2)
                port.name = index == 0 ? "Carrier L" : "Carrier R";
            else
                port.name = index == 2 ? "Modulator L" : "Modulator R";
            port.symbol = String("in_") + String(static_cast<int>(index + 1));
        }
    }

    void initPortGroup(const std::uint32_t groupId, PortGroup& group) override
    {
        if (groupId == 0)
            group.name = "Carrier (analysed)";
        else if (groupId == 1)
            group.name = "Modulator";
    }

    void initParameter(std::uint32_t index, Parameter& parameter) override
    {
        const ParamSpec& spec = kParameterSpecs[index];

        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        if (spec.output)
            parameter.hints = kParameterIsOutput;
        else if (spec.trigger)
            parameter.hints = kParameterIsAutomatable | kParameterIsInteger | kParameterIsTrigger;
        else
            parameter.hints = kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;

        // Ring Freq spans three decades. DPF has no logarithmic range flag, so the
        // wrapper exposes it linearly and the core's bandEdges/quantiseRatio do
        // the musical mapping; the panel draws it on a log track.
    }

    void initState(std::uint32_t index, State& state) override
    {
        static constexpr const char* kKeys[kStateCount] = {
            kStateKeyParameters, kStateKeyMix, kStateKeyBypass, kStateKeyRingFreq,
            kStateKeyRingRatio, kStateKeyRingShape, kStateKeyRingDepth, kStateKeyBandCount,
            kStateKeyBandSpread, kStateKeyAttackMs, kStateKeyReleaseMs, kStateKeyFormantShift,
            kStateKeyTilt, kStateKeyCarrierSource, kStateKeySync, kStateKeyStereoWidth,
            kStateKeyDrift, kStateKeySeed, kStateKeyCCs,
        };
        static constexpr const char* kLabels[kStateCount] = {
            "Parameters", "Mix", "Bypass", "Ring Freq", "Ring Ratio", "Ring Shape",
            "Ring Depth", "Bands", "Spread", "Attack", "Release", "Formant", "Tilt",
            "Carrier", "Sync", "Width", "Drift", "Seed", "CC Map",
        };

        if (index >= kStateCount)
            return;

        state.key = kKeys[index];
        state.label = kLabels[index];
        state.hints = kStateIsOnlyForDSP;

        if (index == 0) {
            state.defaultValue = downspout::voxmod::serializeParameters(core()).c_str();
        } else if (index == kStateCount - 1) {
            state.defaultValue = "1,2,3,4,5,1";
        } else {
            // Every musical control also gets its own state key so a host can
            // surface them individually.
            static thread_local std::string scratch;
            const ParamSpec& spec = kParameterSpecs[indexForState(index)];
            if (spec.integer)
                scratch = std::to_string(static_cast<long>(std::lround(spec.defaultValue)));
            else
                scratch = std::to_string(spec.defaultValue);
            state.defaultValue = scratch.c_str();
        }
    }

    float getParameterValue(std::uint32_t index) const override
    {
        return index < kParameterCount ? values_[index] : 0.0f;
    }

    void setParameterValue(std::uint32_t index, float value) override
    {
        if (index >= kParameterCount)
            return;

        const ParamSpec& spec = kParameterSpecs[index];
        if (spec.output)
            return;  // read-only, the processor owns these

        if (spec.trigger) {
            if (value > 0.5f)
                handleRandomise();
            values_[index] = 0.0f;
            return;
        }

        float v = clampValue(value, spec.minimum, spec.maximum);
        if (spec.integer)
            v = std::round(v);

        if (values_[index] == v)
            return;

        values_[index] = v;
        applyToCore();
    }

    String getState(const char* key) const override
    {
        if (key == nullptr)
            return String();

        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(downspout::voxmod::serializeParameters(core()).c_str());

        const Parameters p = core();
        char buf[64];

        auto num = [&](const float value) {
            std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(value));
            return String(buf);
        };
        auto integer = [&](const float value) {
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
            return String(buf);
        };

        if (std::strcmp(key, kStateKeyMix) == 0) return num(p.mix);
        if (std::strcmp(key, kStateKeyBypass) == 0) return integer(p.bypass);
        if (std::strcmp(key, kStateKeySeed) == 0) return integer(p.seed);
        if (std::strcmp(key, kStateKeyRingFreq) == 0) return num(p.ringFreq);
        if (std::strcmp(key, kStateKeyRingRatio) == 0) return num(p.ringRatio);
        if (std::strcmp(key, kStateKeyRingShape) == 0) return integer(p.ringShape);
        if (std::strcmp(key, kStateKeyRingDepth) == 0) return num(p.ringDepth);
        if (std::strcmp(key, kStateKeyBandCount) == 0) return integer(p.bandCount);
        if (std::strcmp(key, kStateKeyBandSpread) == 0) return num(p.bandSpread);
        if (std::strcmp(key, kStateKeyAttackMs) == 0) return num(p.attackMs);
        if (std::strcmp(key, kStateKeyReleaseMs) == 0) return num(p.releaseMs);
        if (std::strcmp(key, kStateKeyFormantShift) == 0) return num(p.formantShift);
        if (std::strcmp(key, kStateKeyTilt) == 0) return num(p.tilt);
        if (std::strcmp(key, kStateKeyCarrierSource) == 0) return integer(p.carrierSource);
        if (std::strcmp(key, kStateKeySync) == 0) return integer(p.sync);
        if (std::strcmp(key, kStateKeyStereoWidth) == 0) return num(p.stereoWidth);
        if (std::strcmp(key, kStateKeyDrift) == 0) return num(p.drift);

        if (std::strcmp(key, kStateKeyCCs) == 0) {
            std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d",
                          static_cast<int>(values_[kCcMix]), static_cast<int>(values_[kCcRingFreq]),
                          static_cast<int>(values_[kCcRingRatio]), static_cast<int>(values_[kCcBandCount]),
                          static_cast<int>(values_[kCcCarrier]), static_cast<int>(values_[kCcChannel]));
            return String(buf);
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (key == nullptr || value == nullptr)
            return;

        if (std::strcmp(key, kStateKeyParameters) == 0) {
            const auto restored = downspout::voxmod::deserializeParameters(value);
            if (restored.has_value()) {
                writeCore(*restored);
                publishStatus();
            }
            return;
        }

        char* end = nullptr;
        const float f = std::strtof(value, &end);
        if (end == value)
            return;

        Parameters p = core();

        if (std::strcmp(key, kStateKeyMix) == 0) p.mix = f;
        else if (std::strcmp(key, kStateKeyBypass) == 0) p.bypass = f;
        else if (std::strcmp(key, kStateKeySeed) == 0) p.seed = f;
        else if (std::strcmp(key, kStateKeyRingFreq) == 0) p.ringFreq = f;
        else if (std::strcmp(key, kStateKeyRingRatio) == 0) p.ringRatio = f;
        else if (std::strcmp(key, kStateKeyRingShape) == 0) p.ringShape = f;
        else if (std::strcmp(key, kStateKeyRingDepth) == 0) p.ringDepth = f;
        else if (std::strcmp(key, kStateKeyBandCount) == 0) p.bandCount = f;
        else if (std::strcmp(key, kStateKeyBandSpread) == 0) p.bandSpread = f;
        else if (std::strcmp(key, kStateKeyAttackMs) == 0) p.attackMs = f;
        else if (std::strcmp(key, kStateKeyReleaseMs) == 0) p.releaseMs = f;
        else if (std::strcmp(key, kStateKeyFormantShift) == 0) p.formantShift = f;
        else if (std::strcmp(key, kStateKeyTilt) == 0) p.tilt = f;
        else if (std::strcmp(key, kStateKeyCarrierSource) == 0) p.carrierSource = f;
        else if (std::strcmp(key, kStateKeySync) == 0) p.sync = f;
        else if (std::strcmp(key, kStateKeyStereoWidth) == 0) p.stereoWidth = f;
        else if (std::strcmp(key, kStateKeyDrift) == 0) p.drift = f;
        else if (std::strcmp(key, kStateKeyCCs) == 0) {
            int n[6] = {0, 0, 0, 0, 0, 0};
            std::sscanf(value, "%d,%d,%d,%d,%d,%d", &n[0], &n[1], &n[2], &n[3], &n[4], &n[5]);
            values_[kCcMix] = static_cast<float>(std::clamp(n[0], 0, 127));
            values_[kCcRingFreq] = static_cast<float>(std::clamp(n[1], 0, 127));
            values_[kCcRingRatio] = static_cast<float>(std::clamp(n[2], 0, 127));
            values_[kCcBandCount] = static_cast<float>(std::clamp(n[3], 0, 127));
            values_[kCcCarrier] = static_cast<float>(std::clamp(n[4], 0, 127));
            values_[kCcChannel] = static_cast<float>(std::clamp(n[5], 1, 16));
            return;
        }
        else
            return;

        writeCore(p);
        publishStatus();
    }

    void activate() override
    {
        downspout::voxmod::activate(engine_);
        writeCore(core());
        publishStatus();
    }

    void run(const float** inputs, float** outputs, std::uint32_t frames,
             const MidiEvent* midiEvents, std::uint32_t midiEventCount) override
    {
        handleMidi(midiEvents, midiEventCount);

        downspout::voxmod::processBlock(engine_, core(), frames, getSampleRate(),
                                        inputs, outputs);
        publishStatus();
    }

private:
    enum ParameterIndex : std::uint32_t {
        kParamMix = 0,
        kParamBypass,
        kParamSeed,
        kParamRingFreq,
        kParamRingRatio,
        kParamRingShape,
        kParamRingDepth,
        kParamBandCount,
        kParamBandSpread,
        kParamAttackMs,
        kParamReleaseMs,
        kParamFormantShift,
        kParamTilt,
        kParamCarrierSource,
        kParamSync,
        kParamStereoWidth,
        kParamDrift,
        kParamCcMix,
        kParamCcRingFreq,
        kParamCcRingRatio,
        kParamCcBandCount,
        kParamCcCarrier,
        kParamCcChannel,
        kParamRandomise,
        kParamOutCarrierHz,
        kParamOutSibilance,
        kParamOutReduction,
    };

    // These mirror ParamId exactly; the static_asserts keep them honest.
    static constexpr std::uint32_t kCcMix = index(ParamId::ccMix);
    static constexpr std::uint32_t kCcRingFreq = index(ParamId::ccRingFreq);
    static constexpr std::uint32_t kCcRingRatio = index(ParamId::ccRingRatio);
    static constexpr std::uint32_t kCcBandCount = index(ParamId::ccBandCount);
    static constexpr std::uint32_t kCcCarrier = index(ParamId::ccCarrier);
    static constexpr std::uint32_t kCcChannel = index(ParamId::ccChannel);
    static constexpr std::uint32_t kOutCarrierHz = index(ParamId::outCarrierHz);
    static constexpr std::uint32_t kOutSibilance = index(ParamId::outSibilance);
    static constexpr std::uint32_t kOutReduction = index(ParamId::outReduction);

    static_assert(kParamMix == index(ParamId::mix));
    static_assert(kParamBypass == index(ParamId::bypass));
    static_assert(kParamSeed == index(ParamId::seed));
    static_assert(kParamRingFreq == index(ParamId::ringFreq));
    static_assert(kParamRingRatio == index(ParamId::ringRatio));
    static_assert(kParamRingShape == index(ParamId::ringShape));
    static_assert(kParamRingDepth == index(ParamId::ringDepth));
    static_assert(kParamBandCount == index(ParamId::bandCount));
    static_assert(kParamBandSpread == index(ParamId::bandSpread));
    static_assert(kParamAttackMs == index(ParamId::attackMs));
    static_assert(kParamReleaseMs == index(ParamId::releaseMs));
    static_assert(kParamFormantShift == index(ParamId::formantShift));
    static_assert(kParamTilt == index(ParamId::tilt));
    static_assert(kParamCarrierSource == index(ParamId::carrierSource));
    static_assert(kParamSync == index(ParamId::sync));
    static_assert(kParamStereoWidth == index(ParamId::stereoWidth));
    static_assert(kParamDrift == index(ParamId::drift));
    static_assert(kParamRandomise == index(ParamId::randomise));
    static_assert(kParameterCount == 27, "the enum above must cover every spec");

    [[nodiscard]] std::uint32_t ccParameter(const ParamId target) const noexcept
    {
        switch (target) {
        case ParamId::mix: return kCcMix;
        case ParamId::ringFreq: return kCcRingFreq;
        case ParamId::ringRatio: return kCcRingRatio;
        case ParamId::bandCount: return kCcBandCount;
        case ParamId::carrierSource: return kCcCarrier;
        default: return kCcMix;
        }
    }

    // State keys 1..17 map onto the musical parameters in declaration order.
    [[nodiscard]] std::uint32_t indexForState(const std::uint32_t stateIndex) const noexcept
    {
        switch (stateIndex) {
        case 1: return kParamMix;
        case 2: return kParamBypass;
        case 3: return kParamRingFreq;
        case 4: return kParamRingRatio;
        case 5: return kParamRingShape;
        case 6: return kParamRingDepth;
        case 7: return kParamBandCount;
        case 8: return kParamBandSpread;
        case 9: return kParamAttackMs;
        case 10: return kParamReleaseMs;
        case 11: return kParamFormantShift;
        case 12: return kParamTilt;
        case 13: return kParamCarrierSource;
        case 14: return kParamSync;
        case 15: return kParamStereoWidth;
        case 16: return kParamDrift;
        case 17: return kParamSeed;
        default: return 0;
        }
    }

    [[nodiscard]] Parameters core() const noexcept
    {
        Parameters p;
        p.mix = values_[kParamMix];
        p.bypass = values_[kParamBypass];
        p.seed = values_[kParamSeed];
        p.ringFreq = values_[kParamRingFreq];
        p.ringRatio = values_[kParamRingRatio];
        p.ringShape = values_[kParamRingShape];
        p.ringDepth = values_[kParamRingDepth];
        p.bandCount = values_[kParamBandCount];
        p.bandSpread = values_[kParamBandSpread];
        p.attackMs = values_[kParamAttackMs];
        p.releaseMs = values_[kParamReleaseMs];
        p.formantShift = values_[kParamFormantShift];
        p.tilt = values_[kParamTilt];
        p.carrierSource = values_[kParamCarrierSource];
        p.sync = values_[kParamSync];
        p.stereoWidth = values_[kParamStereoWidth];
        p.drift = values_[kParamDrift];
        p.ccMix = values_[kCcMix];
        p.ccRingFreq = values_[kCcRingFreq];
        p.ccRingRatio = values_[kCcRingRatio];
        p.ccBandCount = values_[kCcBandCount];
        p.ccCarrier = values_[kCcCarrier];
        p.ccChannel = values_[kCcChannel];
        return downspout::voxmod::clampParameters(p);
    }

    void writeCore(const Parameters& p) noexcept
    {
        const Parameters c = downspout::voxmod::clampParameters(p);
        values_[kParamMix] = c.mix;
        values_[kParamBypass] = c.bypass;
        values_[kParamSeed] = c.seed;
        values_[kParamRingFreq] = c.ringFreq;
        values_[kParamRingRatio] = c.ringRatio;
        values_[kParamRingShape] = c.ringShape;
        values_[kParamRingDepth] = c.ringDepth;
        values_[kParamBandCount] = c.bandCount;
        values_[kParamBandSpread] = c.bandSpread;
        values_[kParamAttackMs] = c.attackMs;
        values_[kParamReleaseMs] = c.releaseMs;
        values_[kParamFormantShift] = c.formantShift;
        values_[kParamTilt] = c.tilt;
        values_[kParamCarrierSource] = c.carrierSource;
        values_[kParamSync] = c.sync;
        values_[kParamStereoWidth] = c.stereoWidth;
        values_[kParamDrift] = c.drift;
        values_[kCcMix] = c.ccMix;
        values_[kCcRingFreq] = c.ccRingFreq;
        values_[kCcRingRatio] = c.ccRingRatio;
        values_[kCcBandCount] = c.ccBandCount;
        values_[kCcCarrier] = c.ccCarrier;
        values_[kCcChannel] = c.ccChannel;
    }

    void applyToCore() noexcept
    {
        writeCore(core());
    }

    // The Ratio control is quantised to 16 musical steps in the core, so the
    // wrapper reports the value the DSP actually uses rather than the raw one.
    [[nodiscard]] float effectiveRatio() const noexcept
    {
        return downspout::voxmod::quantiseRatio(values_[kParamRingRatio]);
    }

    void handleMidi(const MidiEvent* midiEvents, const std::uint32_t midiEventCount) noexcept
    {
        if (midiEvents == nullptr)
            return;

        const int channel = static_cast<int>(values_[kCcChannel]);

        for (std::uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& ev = midiEvents[i];
            if (ev.size < 3)
                continue;
            const std::uint8_t status = ev.data[0];
            if ((status & 0xf0u) != 0xb0u)
                continue;
            if ((status & 0x0fu) + 1u != static_cast<std::uint8_t>(channel))
                continue;

            const std::uint8_t controller = ev.data[1];
            const std::uint8_t value = ev.data[2];

            for (const auto& mapping : kControllerMap) {
                if (controller != mapping.controller)
                    continue;
                // CC 0 disables the override for that control.
                if (static_cast<int>(values_[ccParameter(mapping.target)]) <= 0)
                    continue;

                const std::uint32_t parameter = index(mapping.target);
                float next = controllerToParameter(parameter, value);
                const ParamSpec& spec = kParameterSpecs[parameter];
                if (spec.integer)
                    next = std::round(next);

                if (values_[parameter] == next)
                    continue;
                values_[parameter] = next;
            }
        }
    }

    void handleRandomise() noexcept
    {
        const Parameters next = downspout::voxmod::randomiseParameters(core());
        writeCore(next);
        // The carrier and the filter bank both jumped, so restart the analysis
        // rather than ringing the old configuration through the transition.
        downspout::voxmod::activate(engine_);
        ++engine_.randomiseSerial;
        publishStatus();
    }

    void publishStatus() noexcept
    {
        values_[kOutCarrierHz] = downspout::voxmod::estimateCarrierHz(core());
        // Sibilance and reduction are measured, not derived from the parameters,
        // so they keep whatever the last processed block found.
        values_[kOutSibilance] = engine_.sibilance;
        values_[kOutReduction] = engine_.reductionDb;
    }

    std::array<float, kParameterCount> values_ {};
    EngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoxmodPlugin)
};

Plugin* createPlugin()
{
    return new VoxmodPlugin();
}

END_NAMESPACE_DISTRHO