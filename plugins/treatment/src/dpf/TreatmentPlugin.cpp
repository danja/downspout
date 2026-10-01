#include "DistrhoPlugin.hpp"

#include "treatment_core_types.hpp"
#include "treatment_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::treatment::EngineState;
using downspout::treatment::Parameters;
using downspout::treatment::PanelState;
using downspout::treatment::ParamId;
using downspout::treatment::ParamSpec;
using downspout::treatment::kControllerMap;
using downspout::treatment::kParameterCount;
using downspout::treatment::kParameterSpecs;

constexpr const char* kStateKeyParameters = "parameters";
constexpr const char* kStateKeyCavity = "cavity";
constexpr const char* kStateKeyGap = "gap";
constexpr const char* kStateKeyMass = "mass";
constexpr const char* kStateKeyResist = "resist";
constexpr const char* kStateKeyAmount = "amount";
constexpr const char* kStateKeyBypass = "bypass";
constexpr const char* kStateKeySeed = "seed";
constexpr const char* kStateKeyCCs = "cc_map";

constexpr std::uint32_t kStateCount = 9;

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

class TreatmentPlugin : public Plugin
{
public:
    TreatmentPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        for (std::size_t i = 0; i < kParameterCount; ++i) {
            if (!kParameterSpecs[i].output)
                values_[i] = kParameterSpecs[i].defaultValue;
        }
        applyToCore();
    }

protected:
    const char* getLabel() const override { return "Treatment"; }
    const char* getDescription() const override
    {
        return "Physically modelled acoustic treatment panel: a mass-air-mass absorber "
               "with a porous fill. Randomise builds a new panel; five CCs drive the geometry.";
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

    int64_t getUniqueId() const override { return d_cconst('T', 'r', 't', 'm'); }

    void initAudioPort(const bool input, const std::uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
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
    }

    void initState(std::uint32_t index, State& state) override
    {
        static constexpr const char* kKeys[kStateCount] = {
            kStateKeyParameters, kStateKeyCavity, kStateKeyGap, kStateKeyMass,
            kStateKeyResist, kStateKeyAmount, kStateKeyBypass, kStateKeySeed, kStateKeyCCs,
        };
        static constexpr const char* kLabels[kStateCount] = {
            "Parameters", "Cavity", "Gap", "Facing Mass", "Flow Resist",
            "Amount", "Bypass", "Seed", "CC Map",
        };

        if (index < kStateCount) {
            state.key = kKeys[index];
            state.label = kLabels[index];
            state.hints = kStateIsOnlyForDSP;
            if (std::strcmp(kKeys[index], kStateKeyParameters) == 0) {
                state.defaultValue = downspout::treatment::serializeParameters(core()).c_str();
            } else if (std::strcmp(kKeys[index], kStateKeyCCs) == 0) {
                state.defaultValue = "1,2,3,4,5,1";
            } else {
                // Each panel property also gets its own state key, so a host can
                // expose them individually. The default is the spec default.
                static thread_local std::string scratch;
                scratch.clear();
                const ParamSpec& spec = kParameterSpecs[indexForState(index)];
                if (spec.integer)
                    scratch = std::to_string(static_cast<long>(std::lround(spec.defaultValue)));
                else
                    scratch = std::to_string(spec.defaultValue);
                state.defaultValue = scratch.c_str();
            }
        }
    }

    float getParameterValue(std::uint32_t index) const override
    {
        if (index < kParameterCount)
            return values_[index];
        return 0.0f;
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
        publishStatus();
    }

    String getState(const char* key) const override
    {
        if (key == nullptr)
            return String();

        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(downspout::treatment::serializeParameters(core()).c_str());

        char buf[16];
        if (std::strcmp(key, kStateKeyCavity) == 0)
            { std::snprintf(buf, sizeof(buf), "%g", core().cavity); return String(buf); }
        if (std::strcmp(key, kStateKeyGap) == 0)
            { std::snprintf(buf, sizeof(buf), "%g", core().gap); return String(buf); }
        if (std::strcmp(key, kStateKeyMass) == 0)
            { std::snprintf(buf, sizeof(buf), "%g", core().mass); return String(buf); }
        if (std::strcmp(key, kStateKeyResist) == 0)
            { std::snprintf(buf, sizeof(buf), "%g", core().resist); return String(buf); }
        if (std::strcmp(key, kStateKeyAmount) == 0)
            { std::snprintf(buf, sizeof(buf), "%g", core().amount); return String(buf); }
        if (std::strcmp(key, kStateKeyBypass) == 0)
            { std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(core().bypass)); return String(buf); }
        if (std::strcmp(key, kStateKeySeed) == 0)
            { std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(core().seed)); return String(buf); }
        if (std::strcmp(key, kStateKeyCCs) == 0) {
            std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d",
                          static_cast<int>(values_[kCcAmount]), static_cast<int>(values_[kCcCavity]),
                          static_cast<int>(values_[kCcGap]), static_cast<int>(values_[kCcMass]),
                          static_cast<int>(values_[kCcResist]), static_cast<int>(values_[kCcChannel]));
            return String(buf);
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (key == nullptr || value == nullptr)
            return;

        if (std::strcmp(key, kStateKeyParameters) == 0) {
            const auto restored = downspout::treatment::deserializeParameters(value);
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
        if (std::strcmp(key, kStateKeyCavity) == 0) p.cavity = f;
        else if (std::strcmp(key, kStateKeyGap) == 0) p.gap = f;
        else if (std::strcmp(key, kStateKeyMass) == 0) p.mass = f;
        else if (std::strcmp(key, kStateKeyResist) == 0) p.resist = f;
        else if (std::strcmp(key, kStateKeyAmount) == 0) p.amount = f;
        else if (std::strcmp(key, kStateKeyBypass) == 0) p.bypass = f;
        else if (std::strcmp(key, kStateKeySeed) == 0) { p.seed = f; writeCore(p); publishStatus(); return; }
        else if (std::strcmp(key, kStateKeyCCs) == 0) {
            int n[6] = {0, 0, 0, 0, 0, 0};
            std::sscanf(value, "%d,%d,%d,%d,%d,%d", &n[0], &n[1], &n[2], &n[3], &n[4], &n[5]);
            values_[kCcAmount] = static_cast<float>(std::clamp(n[0], 0, 127));
            values_[kCcCavity] = static_cast<float>(std::clamp(n[1], 0, 127));
            values_[kCcGap] = static_cast<float>(std::clamp(n[2], 0, 127));
            values_[kCcMass] = static_cast<float>(std::clamp(n[3], 0, 127));
            values_[kCcResist] = static_cast<float>(std::clamp(n[4], 0, 127));
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
        downspout::treatment::activate(engine_);
        writeCore(core());
        publishStatus();
    }

    void run(const float** inputs, float** outputs, std::uint32_t frames,
             const MidiEvent* midiEvents, std::uint32_t midiEventCount) override
    {
        const int channel = static_cast<int>(values_[kCcChannel]);

        bool geometryChanged = false;

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

            Parameters p = core();
            for (const auto& mapping : kControllerMap) {
                if (controller != mapping.controller)
                    continue;
                // CC 0 disables the override for that control.
                if (static_cast<int>(values_[ccParameter(mapping.target)]) <= 0)
                    continue;

                const std::uint32_t parameter = downspout::treatment::index(mapping.target);
                const float next = controllerToParameter(parameter, value);
                if (values_[parameter] == next)
                    continue;

                values_[parameter] = next;
                switch (mapping.target) {
                case ParamId::amount: p.amount = next; break;
                case ParamId::cavity: p.cavity = next; geometryChanged = true; break;
                case ParamId::gap: p.gap = next; geometryChanged = true; break;
                case ParamId::mass: p.mass = next; geometryChanged = true; break;
                case ParamId::resist: p.resist = next; geometryChanged = true; break;
                default: break;
                }
            }
            writeCore(p);
        }

        if (geometryChanged) {
            const PanelState panel = downspout::treatment::analysePanel(core());
            values_[kOutResonance] = panel.resonanceHz;
            values_[kOutAbsorb] = panel.peakAbsorb;
            values_[kOutDiffusion] = panel.diffusionHz;
        }

        downspout::treatment::processBlock(engine_, core(), frames, getSampleRate(),
                                           inputs, outputs);
    }

private:
    enum ParameterIndex : std::uint32_t {
        kParamCavity = 0,
        kParamGap,
        kParamMass,
        kParamResist,
        kParamAmount,
        kParamBypass,
        kParamSeed,
        kParamCcAmount,
        kParamCcCavity,
        kParamCcGap,
        kParamCcMass,
        kParamCcResist,
        kParamCcChannel,
        kParamRandomise,
        kParamOutResonance,
        kParamOutAbsorb,
        kParamOutDiffusion,
    };

    // These mirror ParamId exactly; the static_assert below keeps them honest.
    static constexpr std::uint32_t kCcAmount = downspout::treatment::index(ParamId::ccAmount);
    static constexpr std::uint32_t kCcCavity = downspout::treatment::index(ParamId::ccCavity);
    static constexpr std::uint32_t kCcGap = downspout::treatment::index(ParamId::ccGap);
    static constexpr std::uint32_t kCcMass = downspout::treatment::index(ParamId::ccMass);
    static constexpr std::uint32_t kCcResist = downspout::treatment::index(ParamId::ccResist);
    static constexpr std::uint32_t kCcChannel = downspout::treatment::index(ParamId::ccChannel);
    static constexpr std::uint32_t kOutResonance = downspout::treatment::index(ParamId::outResonance);
    static constexpr std::uint32_t kOutAbsorb = downspout::treatment::index(ParamId::outAbsorb);
    static constexpr std::uint32_t kOutDiffusion = downspout::treatment::index(ParamId::outDiffusion);

    static_assert(kParamCavity == downspout::treatment::index(ParamId::cavity));
    static_assert(kParamGap == downspout::treatment::index(ParamId::gap));
    static_assert(kParamMass == downspout::treatment::index(ParamId::mass));
    static_assert(kParamResist == downspout::treatment::index(ParamId::resist));
    static_assert(kParamAmount == downspout::treatment::index(ParamId::amount));
    static_assert(kParamBypass == downspout::treatment::index(ParamId::bypass));
    static_assert(kParamSeed == downspout::treatment::index(ParamId::seed));
    static_assert(kParamRandomise == downspout::treatment::index(ParamId::randomise));
    static_assert(kParameterCount == 17, "the enum above must cover every spec");

    [[nodiscard]] std::uint32_t ccParameter(const ParamId target) const noexcept
    {
        switch (target) {
        case ParamId::amount: return kCcAmount;
        case ParamId::cavity: return kCcCavity;
        case ParamId::gap: return kCcGap;
        case ParamId::mass: return kCcMass;
        case ParamId::resist: return kCcResist;
        default: return kCcAmount;
        }
    }

    [[nodiscard]] std::uint32_t indexForState(const std::uint32_t stateIndex) const noexcept
    {
        switch (stateIndex) {
        case 1: return kParamCavity;
        case 2: return kParamGap;
        case 3: return kParamMass;
        case 4: return kParamResist;
        case 5: return kParamAmount;
        case 6: return kParamBypass;
        case 7: return kParamSeed;
        default: return 0;
        }
    }

    [[nodiscard]] Parameters core() const noexcept
    {
        Parameters p;
        p.cavity = values_[kParamCavity];
        p.gap = values_[kParamGap];
        p.mass = values_[kParamMass];
        p.resist = values_[kParamResist];
        p.amount = values_[kParamAmount];
        p.bypass = values_[kParamBypass];
        p.seed = values_[kParamSeed];
        return downspout::treatment::clampParameters(p);
    }

    void writeCore(const Parameters& p) noexcept
    {
        const Parameters c = downspout::treatment::clampParameters(p);
        values_[kParamCavity] = c.cavity;
        values_[kParamGap] = c.gap;
        values_[kParamMass] = c.mass;
        values_[kParamResist] = c.resist;
        values_[kParamAmount] = c.amount;
        values_[kParamBypass] = c.bypass;
        values_[kParamSeed] = c.seed;
    }

    void applyToCore() noexcept
    {
        Parameters p = core();
        const Parameters c = downspout::treatment::clampParameters(p);
        writeCore(c);
    }

    void handleRandomise() noexcept
    {
        const Parameters next = downspout::treatment::randomiseParameters(core());
        writeCore(next);
        // The panel geometry jumped, so let the filters re-converge from a clean
        // state rather than ringing the old geometry through the transition.
        downspout::treatment::activate(engine_);
        ++engine_.randomiseSerial;
        publishStatus();
    }

    void publishStatus() noexcept
    {
        const PanelState panel = downspout::treatment::analysePanel(core());
        values_[kOutResonance] = panel.resonanceHz;
        values_[kOutAbsorb] = panel.peakAbsorb;
        values_[kOutDiffusion] = panel.diffusionHz;
    }

    std::array<float, kParameterCount> values_ {};
    EngineState engine_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TreatmentPlugin)
};

Plugin* createPlugin()
{
    return new TreatmentPlugin();
}

END_NAMESPACE_DISTRHO
