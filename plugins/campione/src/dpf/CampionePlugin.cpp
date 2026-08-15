#include "DistrhoPlugin.hpp"

#include "campione_engine.hpp"
#include "campione_params.hpp"
#include "campione_pitch_utils.hpp"
#include "campione_sample_loader.hpp"
#include "campione_serialization.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

using CoreAudioBlock   = downspout::campione::AudioBlock;
using CoreEngineState  = downspout::campione::EngineState;
using CoreMidiEvent    = downspout::campione::MidiInputEvent;
using CoreParameters   = downspout::campione::Parameters;
using CoreSampleZone   = downspout::campione::SampleZone;
using ZoneVec          = std::vector<CoreSampleZone>;

using downspout::campione::kParamCrossfadeDuration;
using downspout::campione::kParamMasterVolume;
using downspout::campione::kParamMidiChannel;
using downspout::campione::kParamPitchBendRange;
using downspout::campione::kParameterCount;
using downspout::campione::kStateCount;
using downspout::campione::kStateKeyParameters;
using downspout::campione::kStateKeyZoneLoad;
using downspout::campione::kStateKeyZones;
using downspout::campione::kStateParameters;
using downspout::campione::kStateZoneLoad;
using downspout::campione::kStateZones;

ParameterEnumerationValue kMidiChannelEnumValues[] = {
    { 0.0f, "All" },
    { 1.0f, "1" },  { 2.0f, "2" },  { 3.0f, "3" },  { 4.0f, "4" },
    { 5.0f, "5" },  { 6.0f, "6" },  { 7.0f, "7" },  { 8.0f, "8" },
    { 9.0f, "9" },  {10.0f, "10"}, {11.0f, "11"}, {12.0f, "12"},
    {13.0f, "13"}, {14.0f, "14"}, {15.0f, "15"}, {16.0f, "16"},
};

}  // namespace

class CampionePlugin : public Plugin
{
public:
    CampionePlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::campione::clampParameters(parameters_);
    }

protected:
    const char* getLabel()       const override { return "Campione"; }
    const char* getMaker()       const override { return "danja"; }
    const char* getLicense()     const override { return "MIT"; }
    const char* getHomePage()    const override { return "https://danja.github.io/downspout/"; }
    uint32_t    getVersion()     const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('C', 'm', 'p', 'n'); }

    const char* getDescription() const override
    {
        return "Multi-zone sampler with per-note mapping, pitch-shift gap fill, "
               "zero-crossing loop snap, and crossfade looping.";
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input "  : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_"     : "out_")    + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;
        switch (index)
        {
        case kParamMasterVolume:
            parameter.name   = "Volume";
            parameter.symbol = "volume";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.8f;
            break;
        case kParamMidiChannel:
            parameter.name   = "MIDI Channel";
            parameter.symbol = "midi_channel";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = 17;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kMidiChannelEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamCrossfadeDuration:
            parameter.name   = "Crossfade ms";
            parameter.symbol = "crossfade_ms";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 500.0f;
            parameter.ranges.def = 20.0f;
            break;
        case kParamPitchBendRange:
            parameter.name   = "Pitch Bend Range";
            parameter.symbol = "pitch_bend_range";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 24.0f;
            parameter.ranges.def = 2.0f;
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamMasterVolume:    return parameters_.masterVolume;
        case kParamMidiChannel:     return parameters_.midiChannel;
        case kParamCrossfadeDuration: return parameters_.crossfadeDurationMs;
        case kParamPitchBendRange:  return parameters_.pitchBendRange;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamMasterVolume:      parameters_.masterVolume       = value; break;
        case kParamMidiChannel:       parameters_.midiChannel        = value; break;
        case kParamCrossfadeDuration: parameters_.crossfadeDurationMs = value; break;
        case kParamPitchBendRange:    parameters_.pitchBendRange     = value; break;
        default: break;
        }
        parameters_ = downspout::campione::clampParameters(parameters_);
    }

    void initState(uint32_t index, State& state) override
    {
        switch (index)
        {
        case kStateParameters:
            state.key          = kStateKeyParameters;
            state.label        = "Parameters";
            state.hints        = kStateIsOnlyForDSP;
            state.defaultValue = "";
            break;
        case kStateZones:
            state.key          = kStateKeyZones;
            state.label        = "Zones";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZoneLoad:
            state.key          = kStateKeyZoneLoad;
            state.label        = "Load Zone";
            state.hints        = kStateIsFilenamePath;
            state.defaultValue = "";
            break;
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(downspout::campione::serializeParameters(parameters_).c_str());

        if (std::strcmp(key, kStateKeyZones) == 0)
        {
            const auto zones = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            if (!zones) return String();
            return String(downspout::campione::serializeZones(*zones).c_str());
        }

        // zone_load is transient — never persisted
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
        {
            const std::string text = value ? value : "";
            const auto p = downspout::campione::deserializeParameters(text);
            if (p.has_value()) parameters_ = *p;
            return;
        }

        if (std::strcmp(key, kStateKeyZones) == 0)
        {
            restoreZones(value ? value : "");
            return;
        }

        if (std::strcmp(key, kStateKeyZoneLoad) == 0 && value && value[0] != '\0')
        {
            appendZone(value);
            return;
        }
    }

    void activate() override
    {
        downspout::campione::activate(engineState_);
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        // Clear outputs
        for (uint32_t ch = 0; ch < DISTRHO_PLUGIN_NUM_OUTPUTS; ++ch)
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);

        const auto zonesPtr = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        const ZoneVec& zones = zonesPtr ? *zonesPtr : emptyZones_;

        // Convert DPF MIDI events to core format (stack-allocated for RT safety)
        constexpr uint32_t kMaxMidi = 512;
        CoreMidiEvent coreMidi[kMaxMidi];
        const uint32_t midiCount = midiEventCount < kMaxMidi ? midiEventCount : kMaxMidi;
        for (uint32_t i = 0; i < midiCount; ++i)
        {
            const MidiEvent& src = midiEvents[i];
            const uint8_t* data = src.size > MidiEvent::kDataSize ? src.dataExt : src.data;
            coreMidi[i].frame = src.frame;
            coreMidi[i].size  = static_cast<uint8_t>(src.size < 4 ? src.size : 4u);
            for (uint8_t b = 0; b < coreMidi[i].size; ++b)
                coreMidi[i].data[b] = data[b];
        }

        CoreAudioBlock audio;
        audio.outputs[0]   = outputs[0];
        audio.outputs[1]   = outputs[1];
        audio.channelCount = DISTRHO_PLUGIN_NUM_OUTPUTS;

        downspout::campione::processBlock(engineState_, parameters_, zones,
                                          coreMidi, midiCount, frames,
                                          getSampleRate(), audio);

        // Phase 2: audio input capture for recording would go here
        (void)inputs;
    }

private:
    // Load a single WAV zone and append it to the zone list.
    void appendZone(const std::string& path)
    {
        auto result = downspout::campione::loadWavZone(path);
        if (!result.error.empty()) return;

        result.zone.sourcePath = path;
        result.zone.rootNote   = 60; // default; detect below

        // Auto-detect root note from pitch
        const double hz = downspout::campione::detectFundamentalHz(
            result.zone.data, result.zone.channelCount, result.zone.sampleRate);
        const double midiNote = downspout::campione::freqToMidi(hz);
        const int detected = static_cast<int>(std::lround(midiNote));
        if (detected >= 0 && detected <= 127)
            result.zone.rootNote = detected;

        // Set loop range to full sample, snap to zero crossings
        downspout::campione::applyLoopPoints(result.zone, parameters_.crossfadeDurationMs);

        const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        auto newZones = std::make_shared<ZoneVec>(existing ? *existing : ZoneVec{});
        newZones->push_back(std::move(result.zone));
        std::atomic_store_explicit(&zones_,
                                   std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                   std::memory_order_release);
    }

    // Deserialize zone metadata and reload each WAV from its stored path.
    void restoreZones(const std::string& text)
    {
        const auto metas = downspout::campione::deserializeZones(text);
        if (!metas.has_value()) return;

        auto newZones = std::make_shared<ZoneVec>();
        newZones->reserve(metas->size());

        for (const CoreSampleZone& meta : *metas)
        {
            if (meta.sourcePath.empty()) continue;
            auto result = downspout::campione::loadWavZone(meta.sourcePath);
            if (!result.error.empty()) continue;

            // Restore user-edited metadata over freshly loaded WAV
            result.zone.rootNote        = meta.rootNote;
            result.zone.rangeLow        = meta.rangeLow;
            result.zone.rangeHigh       = meta.rangeHigh;
            result.zone.midiChannel     = meta.midiChannel;
            result.zone.loopEnabled     = meta.loopEnabled;
            result.zone.loopStart       = meta.loopStart;
            result.zone.loopEnd         = meta.loopEnd;
            result.zone.crossfadeFrames = meta.crossfadeFrames;
            result.zone.sourcePath      = meta.sourcePath;

            newZones->push_back(std::move(result.zone));
        }

        std::atomic_store_explicit(&zones_,
                                   std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                   std::memory_order_release);
    }

    CoreParameters parameters_ {};
    CoreEngineState engineState_ {};
    // Atomic shared_ptr (C++11 free functions) — cross-thread zone handoff without locks.
    std::shared_ptr<const ZoneVec> zones_ {};
    const ZoneVec emptyZones_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CampionePlugin)
};

Plugin* createPlugin()
{
    return new CampionePlugin();
}

END_NAMESPACE_DISTRHO
