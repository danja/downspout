#include "DistrhoPlugin.hpp"

#include "campione_engine.hpp"
#include "campione_mcp_server.hpp"
#include "campione_params.hpp"
#include "campione_patch_io.hpp"
#include "campione_ui_bridge.hpp"
#include "campione_pitch_utils.hpp"
#include "campione_sample_loader.hpp"
#include "campione_serialization.hpp"
#include "campione_wave_edit.hpp"
#include "campione_drum_map.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
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
using downspout::campione::kParamMcpEnabled;
using downspout::campione::kParamRecording;
using downspout::campione::kParamZonesVersion;
using downspout::campione::kParameterCount;
using downspout::campione::kStateCount;
using downspout::campione::kStateKeyParameters;
using downspout::campione::kStateKeyZoneLoad;
using downspout::campione::kStateKeyZoneFade;
using downspout::campione::kStateKeyZoneNormalize;
using downspout::campione::kStateKeyZoneRemove;
using downspout::campione::kStateKeyZoneReverse;
using downspout::campione::kStateKeyZoneTrim;
using downspout::campione::kStateKeyZoneUpdate;
using downspout::campione::kStateKeyZones;
using downspout::campione::kStateKeyZoneDsp;
using downspout::campione::kStateKeyZoneSlice;
using downspout::campione::kStateKeyZoneReorder;
using downspout::campione::kStateKeyPatchSave;
using downspout::campione::kStateKeyPatchLoad;
using downspout::campione::kStateKeyDataDir;
using downspout::campione::kStateZoneSlice;
using downspout::campione::kStateZoneReorder;
using downspout::campione::kStatePatchSave;
using downspout::campione::kStatePatchLoad;
using downspout::campione::kStateDataDir;
using downspout::campione::kStateZonePreview;
using downspout::campione::kStateKeyZonePreview;
using downspout::campione::kStateKeyWavetableImport;
using downspout::campione::kStateWavetableImport;
using downspout::campione::kStateKeyZoneClear;
using downspout::campione::kStateZoneClear;
using downspout::campione::kDefaultDataDir;
using downspout::campione::kStateParameters;
using downspout::campione::kStateZoneFade;
using downspout::campione::kStateZoneLoad;
using downspout::campione::kStateZoneNormalize;
using downspout::campione::kStateZoneRemove;
using downspout::campione::kStateZoneReverse;
using downspout::campione::kStateZoneTrim;
using downspout::campione::kStateZoneUpdate;
using downspout::campione::kStateZones;
using downspout::campione::kStateZoneDsp;

// Default MCP port; increments up to +9 on conflict.
constexpr int kMcpDefaultPort = 7220;

static std::string defaultDataDir()
{
    const char* h = std::getenv("HOME");
    return std::string(h ? h : "/tmp") + kDefaultDataDir;
}

ParameterEnumerationValue kMidiChannelEnumValues[] = {
    { 0.0f, "All" },
    { 1.0f, "1" },  { 2.0f, "2" },  { 3.0f, "3" },  { 4.0f, "4" },
    { 5.0f, "5" },  { 6.0f, "6" },  { 7.0f, "7" },  { 8.0f, "8" },
    { 9.0f, "9" },  {10.0f, "10"}, {11.0f, "11"}, {12.0f, "12"},
    {13.0f, "13"}, {14.0f, "14"}, {15.0f, "15"}, {16.0f, "16"},
};

// Build a minimal JSON object string for zone inspection.
std::string zonesAsJson(const ZoneVec& zones)
{
    std::string s = "{\"zones\":[";
    for (std::size_t i = 0; i < zones.size(); ++i) {
        const auto& z = zones[i];
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "{\"index\":%zu,\"root_note\":%d,\"range_low\":%d,\"range_high\":%d,"
            "\"loop_enabled\":%s,\"loop_start\":%u,\"loop_end\":%u,"
            "\"total_frames\":%zu,\"source_path\":\"%s\"}",
            i, z.rootNote, z.rangeLow, z.rangeHigh,
            z.loopEnabled ? "true" : "false",
            z.loopStart, z.loopEnd,
            z.data.size() / std::max(1, z.channelCount),
            z.sourcePath.c_str());
        if (i > 0) s += ',';
        s += buf;
    }
    s += "]}";
    return s;
}

std::string paramsAsJson(const CoreParameters& p)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "{\"master_volume\":%.4f,\"midi_channel\":%.0f,"
        "\"crossfade_ms\":%.2f,\"pitch_bend_range\":%.2f}",
        p.masterVolume, p.midiChannel, p.crossfadeDurationMs, p.pitchBendRange);
    return buf;
}

}  // namespace

class CampionePlugin : public Plugin
{
public:
    CampionePlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::campione::clampParameters(parameters_);
        // MCP start is deferred to activate() so only the audio processor
        // component starts a server — not the separate edit-controller instance
        // or REAPER's scan instance, both of which never call activate().
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
               "zero-crossing loop snap, and crossfade looping. "
               "MCP server on port 7220 (see get_port tool).";
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
        case kParamRecording:
            parameter.name   = "Recording";
            parameter.symbol = "recording";
            parameter.hints  = kParameterIsBoolean | kParameterIsHidden;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamMcpEnabled:
            parameter.name   = "MCP Server";
            parameter.symbol = "mcp_enabled";
            parameter.hints  = kParameterIsBoolean | kParameterIsAutomatable;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 1.0f;
            break;
        case kParamZonesVersion:
            parameter.name   = "Zones Version";
            parameter.symbol = "zones_version";
            parameter.hints  = kParameterIsHidden;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1000000.0f;
            parameter.ranges.def = 0.0f;
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamMasterVolume:      return parameters_.masterVolume;
        case kParamMidiChannel:       return parameters_.midiChannel;
        case kParamCrossfadeDuration: return parameters_.crossfadeDurationMs;
        case kParamPitchBendRange:    return parameters_.pitchBendRange;
        case kParamRecording:         return recording_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        case kParamMcpEnabled:        return mcpEnabled_ ? 1.0f : 0.0f;
        case kParamZonesVersion:      return static_cast<float>(zonesVersion_.load(std::memory_order_relaxed));
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamMasterVolume:      parameters_.masterVolume        = value; break;
        case kParamMidiChannel:       parameters_.midiChannel         = value; break;
        case kParamCrossfadeDuration: parameters_.crossfadeDurationMs = value; break;
        case kParamPitchBendRange:    parameters_.pitchBendRange      = value; break;
        case kParamMcpEnabled:
        {
            const bool enable = value >= 0.5f;
            mcpEnabled_ = enable;
            if (enable && mcpStarted_ && !mcp_) {
                mcp_ = std::make_unique<downspout::campione::CampioneMcpServer>(
                    kMcpDefaultPort, buildMcpApi());
                mcp_->start();
            } else if (!enable && mcp_) {
                mcp_->stop();
                mcp_.reset();
            }
            return;
        }
        case kParamRecording:
        {
            const bool arm = value >= 0.5f;
            if (arm && !recording_.load(std::memory_order_acquire)) {
                recordChannels_ = DISTRHO_PLUGIN_NUM_INPUTS;
                recordBuffer_.assign(kMaxRecordSamples, 0.0f);
                recordWritePos_.store(0, std::memory_order_release);
                recording_.store(true, std::memory_order_release);
            } else if (!arm && recording_.load(std::memory_order_acquire)) {
                recording_.store(false, std::memory_order_release);
                // No channel to report errors on a host parameter change.
                (void) finalizeRecording();
            }
            return;
        }
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
            state.hints        = 0;
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
        case kStateZoneRemove:
            state.key          = kStateKeyZoneRemove;
            state.label        = "Remove Zone";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZoneUpdate:
            state.key          = kStateKeyZoneUpdate;
            state.label        = "Update Zone";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZoneNormalize:
            state.key          = kStateKeyZoneNormalize;
            state.label        = "Normalize Zone";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZoneTrim:
            state.key          = kStateKeyZoneTrim;
            state.label        = "Trim Zone";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZoneFade:
            state.key          = kStateKeyZoneFade;
            state.label        = "Fade Zone";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZoneReverse:
            state.key          = kStateKeyZoneReverse;
            state.label        = "Reverse Zone";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZoneDsp:
            state.key          = kStateKeyZoneDsp;
            state.label        = "Zone DSP";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZoneSlice:
            state.key          = kStateKeyZoneSlice;
            state.label        = "Slice Zone";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZoneReorder:
            state.key          = kStateKeyZoneReorder;
            state.label        = "Reorder Zone";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStatePatchSave:
            state.key          = kStateKeyPatchSave;
            state.label        = "Save Patch";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStatePatchLoad:
            state.key          = kStateKeyPatchLoad;
            state.label        = "Load Patch";
            state.hints        = kStateIsFilenamePath;
            state.defaultValue = "";
            break;
        case kStateDataDir:
            state.key          = kStateKeyDataDir;
            state.label        = "Data Directory";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateZonePreview:
            state.key          = kStateKeyZonePreview;
            state.label        = "Preview Zone";
            state.hints        = 0;
            state.defaultValue = "";
            break;
        case kStateWavetableImport:
            state.key          = kStateKeyWavetableImport;
            state.label        = "Import Wavetable";
            state.hints        = kStateIsFilenamePath;
            state.defaultValue = "";
            break;
        case kStateZoneClear:
            state.key          = kStateKeyZoneClear;
            state.label        = "Clear Zones";
            state.hints        = 0;
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

        if (std::strcmp(key, kStateKeyPatchLoad) == 0)
            return String(lastPatchPath_.c_str());

        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
        {
            const auto p = downspout::campione::deserializeParameters(value ? value : "");
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
            notifyZonesChanged(true);
            return;
        }

        if (std::strcmp(key, kStateKeyZoneUpdate) == 0 && value && value[0] != '\0')
        {
            int idx = 0, rootNote = 60, rangeLow = 0, rangeHigh = 127, loopEn = 0, octaveShift = 0;
            unsigned int loopStart = 0, loopEnd = 0;
            const int parsed = std::sscanf(value, "%d|%d|%d|%d|%d|%u|%u|%d",
                                           &idx, &rootNote, &rangeLow, &rangeHigh, &loopEn,
                                           &loopStart, &loopEnd, &octaveShift);
            if (parsed >= 5)
            {
                std::lock_guard<std::mutex> lk(zoneMtx_);
                zonesInitialized_ = true;
                const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
                if (existing && idx >= 0 && idx < static_cast<int>(existing->size())) {
                    auto newZones = std::make_shared<ZoneVec>(*existing);
                    auto& z       = (*newZones)[static_cast<std::size_t>(idx)];
                    z.rootNote    = rootNote;
                    z.rangeLow    = rangeLow;
                    z.rangeHigh   = rangeHigh;
                    z.loopEnabled = loopEn != 0;
                    if (parsed >= 8) z.octaveShift = std::clamp(octaveShift, -6, 6);
                    if (parsed >= 7) {
                        z.loopStart = static_cast<std::uint32_t>(loopStart);
                        z.loopEnd   = static_cast<std::uint32_t>(loopEnd);
                        const std::uint32_t loopLen = z.loopEnd > z.loopStart
                                                      ? z.loopEnd - z.loopStart : 1;
                        // Preserve the zone's existing crossfadeFrames (set on load);
                        // only cap it if the loop region was shortened by the UI drag.
                        if (z.crossfadeFrames > loopLen / 2) z.crossfadeFrames = loopLen / 2;
                    } else {
                        downspout::campione::applyLoopPoints(z, parameters_.crossfadeDurationMs);
                    }
                    std::atomic_store_explicit(&zones_,
                                               std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                               std::memory_order_release);
                    notifyZonesChanged();
                }
            }
            return;
        }

        if (std::strcmp(key, kStateKeyZoneRemove) == 0 && value && value[0] != '\0')
        {
            const int idx = std::atoi(value);
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            if (existing && idx >= 0 && idx < static_cast<int>(existing->size())) {
                auto newZones = std::make_shared<ZoneVec>(*existing);
                newZones->erase(newZones->begin() + idx);
                std::atomic_store_explicit(&zones_,
                                           std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                           std::memory_order_release);
                notifyZonesChanged(true);
            }
            return;
        }

        if (std::strcmp(key, kStateKeyZoneNormalize) == 0 && value && value[0] != '\0')
        {
            mcpEditZone(std::atoi(value), [](CoreSampleZone& z) {
                return downspout::campione::normalizeZone(z);
            });
            return;
        }

        if (std::strcmp(key, kStateKeyZoneTrim) == 0 && value && value[0] != '\0')
        {
            int idx = 0; float thr = -60.0f;
            std::sscanf(value, "%d|%f", &idx, &thr);
            const float lin = std::pow(10.0f, thr / 20.0f);
            mcpEditZone(idx, [lin](CoreSampleZone& z) -> std::string {
                downspout::campione::trimZone(z, lin);
                return {};
            });
            return;
        }

        if (std::strcmp(key, kStateKeyZoneFade) == 0 && value && value[0] != '\0')
        {
            int idx = 0; float inMs = 10.0f, outMs = 10.0f;
            std::sscanf(value, "%d|%f|%f", &idx, &inMs, &outMs);
            mcpEditZone(idx, [inMs, outMs](CoreSampleZone& z) -> std::string {
                const auto inF  = static_cast<uint32_t>((inMs  / 1000.0f) * z.sampleRate);
                const auto outF = static_cast<uint32_t>((outMs / 1000.0f) * z.sampleRate);
                downspout::campione::fadeZone(z, inF, outF);
                return {};
            });
            return;
        }

        if (std::strcmp(key, kStateKeyZoneReverse) == 0 && value && value[0] != '\0')
        {
            mcpEditZone(std::atoi(value), [](CoreSampleZone& z) -> std::string {
                downspout::campione::reverseZone(z);
                return {};
            });
            return;
        }

        if (std::strcmp(key, kStateKeyZoneDsp) == 0 && value && value[0] != '\0')
        {
            int idx = 0, filterEnabled = 0, filterType = 0, muted = 0;
            float attackMs = 5.0f, decayMs = 100.0f, sustainLevel = 1.0f, releaseMs = 200.0f;
            float filterCutoff = 20000.0f, filterQ = 0.707f, pan = 0.0f;
            const int parsed = std::sscanf(value, "%d|%f|%f|%f|%f|%d|%d|%f|%f|%f|%d",
                                           &idx, &attackMs, &decayMs, &sustainLevel, &releaseMs,
                                           &filterEnabled, &filterType, &filterCutoff, &filterQ,
                                           &pan, &muted);
            if (parsed >= 5)
            {
                std::lock_guard<std::mutex> lk(zoneMtx_);
                zonesInitialized_ = true;
                const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
                if (existing && idx >= 0 && idx < static_cast<int>(existing->size())) {
                    auto newZones = std::make_shared<ZoneVec>(*existing);
                    auto& z = (*newZones)[static_cast<std::size_t>(idx)];
                    z.attackMs      = attackMs;
                    z.decayMs       = decayMs;
                    z.sustainLevel  = std::clamp(sustainLevel, 0.0f, 1.0f);
                    z.releaseMs     = releaseMs;
                    if (parsed >= 7) { z.filterEnabled = filterEnabled != 0; z.filterType = filterType; }
                    if (parsed >= 9) { z.filterCutoffHz = filterCutoff; z.filterQ = filterQ; }
                    if (parsed >= 10) { z.pan = std::clamp(pan, -1.0f, 1.0f); }
                    if (parsed >= 11) { z.muted = muted != 0; }
                    std::atomic_store_explicit(&zones_,
                                               std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                               std::memory_order_release);
                    notifyZonesChanged();
                }
            }
            return;
        }

        if (std::strcmp(key, kStateKeyZoneSlice) == 0 && value && value[0] != '\0')
        {
            int idx = 0, numSlices = 0, startNote = -1;
            std::sscanf(value, "%d|%d|%d", &idx, &numSlices, &startNote);
            doSliceZone(idx, numSlices, startNote);
            return;
        }

        if (std::strcmp(key, kStateKeyZoneReorder) == 0 && value && value[0] != '\0')
        {
            int from = 0, to = 0;
            if (std::sscanf(value, "%d|%d", &from, &to) == 2)
                doZoneReorder(from, to);
            return;
        }

        if (std::strcmp(key, kStateKeyPatchSave) == 0)
        {
            // "auto" sentinel (or empty) → auto-generate path; otherwise use explicit path
            const bool autoPath = (!value || value[0] == '\0' || std::strcmp(value, "auto") == 0);
            doSavePatch(autoPath ? "" : value);
            return;
        }

        if (std::strcmp(key, kStateKeyPatchLoad) == 0 && value && value[0] != '\0')
        {
            // Skip reload if zones are already initialised from this same path (host
            // re-sends state on UI reopen; reloading would trigger an unnecessary restart).
            {
                std::lock_guard<std::mutex> lk(zoneMtx_);
                if (zonesInitialized_ && lastPatchPath_ == value) return;
            }
            doLoadPatch(value);
            return;
        }

        if (std::strcmp(key, kStateKeyDataDir) == 0 && value && value[0] != '\0')
        {
            ::mkdir(value, 0755);
            struct stat st{};
            if (::stat(value, &st) == 0 && S_ISDIR(st.st_mode))
                recordingOutputDir_ = value;
            // Load auto-save if REAPER hasn't provided zones from a saved project.
            // This makes zone state independent of whether the host project was saved.
            if (!zonesInitialized_) {
                const std::string autoSave = recordingOutputDir_ + "/campione_autosave.ttl";
                struct stat ast{};
                if (::stat(autoSave.c_str(), &ast) == 0)
                    doLoadPatch(autoSave);
            }
            return;
        }

        if (std::strcmp(key, kStateKeyZonePreview) == 0 && value)
        {
            pendingPreviewZone_.store(std::atoi(value), std::memory_order_release);
            return;
        }

        if (std::strcmp(key, kStateKeyZoneClear) == 0)
        {
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::make_shared<ZoneVec>()),
                                       std::memory_order_release);
            notifyZonesChanged(true);
            return;
        }

        if (std::strcmp(key, kStateKeyWavetableImport) == 0 && value && value[0] != '\0')
        {
            const std::string dir = recordingOutputDir_.empty() ? defaultDataDir() : recordingOutputDir_;
            ::mkdir(dir.c_str(), 0755);
            auto result = downspout::campione::importWavetableZone(value, dir);
            if (!result.error.empty()) return;

            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            auto newZones = std::make_shared<ZoneVec>(existing ? *existing : ZoneVec{});
            newZones->push_back(std::move(result.zone));
            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                       std::memory_order_release);
            notifyZonesChanged(true);
            return;
        }
    }

    void activate() override
    {
        downspout::campione::activate(engineState_);
        // Start MCP server on first activation — this only runs on the audio
        // processor component, never on the controller or scan instances.
        if (!mcpStarted_ && mcpEnabled_) {
            mcp_ = std::make_unique<downspout::campione::CampioneMcpServer>(
                kMcpDefaultPort, buildMcpApi());
            mcp_->start();
            mcpStarted_ = true;
        }
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        // Apply MCP-sourced parameter updates (non-blocking try_lock)
        if (mcpParamsMtx_.try_lock()) {
            if (hasMcpParams_) {
                parameters_ = mcpPendingParams_;
                hasMcpParams_ = false;
            }
            mcpParamsMtx_.unlock();
        }

        // Clear outputs
        for (uint32_t ch = 0; ch < DISTRHO_PLUGIN_NUM_OUTPUTS; ++ch)
            if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + frames, 0.0f);

        const auto zonesPtr = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        const ZoneVec& zones = zonesPtr ? *zonesPtr : emptyZones_;

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

        // Synthetic preview injection (UI play button → pendingPreviewZone_ → note event)
        uint32_t coreMidiCount = midiCount;
        {
            const int pending = pendingPreviewZone_.exchange(-2, std::memory_order_acq_rel);
            if (pending >= -1 && coreMidiCount + 2 <= kMaxMidi)
            {
                // Stop any previous preview note
                if (previewNote_ >= 0)
                {
                    CoreMidiEvent& off = coreMidi[coreMidiCount++];
                    off.frame   = 0;
                    off.size    = 3;
                    off.data[0] = 0x80;  // note-off ch1
                    off.data[1] = static_cast<uint8_t>(previewNote_);
                    off.data[2] = 0;
                    previewNote_ = -1;
                }
                if (pending >= 0 && pending < static_cast<int>(zones.size()))
                {
                    // Use zone's root note for preview
                    const int note = zones[static_cast<std::size_t>(pending)].rootNote;
                    CoreMidiEvent& on = coreMidi[coreMidiCount++];
                    on.frame   = 0;
                    on.size    = 3;
                    on.data[0] = 0x90;  // note-on ch1
                    on.data[1] = static_cast<uint8_t>(note);
                    on.data[2] = 100;
                    previewNote_ = note;
                }
            }
        }

        CoreAudioBlock audio;
        audio.outputs[0]   = outputs[0];
        audio.outputs[1]   = outputs[1];
        audio.channelCount = DISTRHO_PLUGIN_NUM_OUTPUTS;

        downspout::campione::processBlock(engineState_, parameters_, zones,
                                          coreMidi, coreMidiCount, frames,
                                          getSampleRate(), audio);

        if (pendingZoneUpdate_.exchange(false, std::memory_order_acq_rel)) {
            const auto zp = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            const std::string serialized = zp
                ? downspout::campione::serializeZones(*zp) : std::string{};
            updateStateValue(kStateKeyZones, serialized.c_str());
            // Bump the hidden zones-version parameter so the host relays a
            // parameterChanged() call to the UI — the only reliable DSP→UI push in VST3.
            const float v = static_cast<float>(++zonesVersion_);
            requestParameterValueChange(kParamZonesVersion, v);
        }

        if (pendingParamNotify_.exchange(false, std::memory_order_acq_rel)) {
            const std::string serialized = downspout::campione::serializeParameters(parameters_);
            updateStateValue(kStateKeyParameters, serialized.c_str());
            // Push to in-process bridge for UI polling.
            {
                std::lock_guard<std::mutex> lk(downspout::campione::uiBridge().paramsMtx);
                auto& b = downspout::campione::uiBridge();
                b.masterVolume    = parameters_.masterVolume;
                b.midiChannel     = parameters_.midiChannel;
                b.crossfadeMs     = parameters_.crossfadeDurationMs;
                b.pitchBendRange  = parameters_.pitchBendRange;
            }
            downspout::campione::uiBridge().paramsSerial.fetch_add(1, std::memory_order_release);
        }

        if (recording_.load(std::memory_order_acquire)) {
            std::size_t pos      = recordWritePos_.load(std::memory_order_relaxed);
            const std::size_t ch = static_cast<std::size_t>(recordChannels_);
            const std::size_t avail = (kMaxRecordSamples > pos) ? kMaxRecordSamples - pos : 0u;
            const std::size_t toWrite = std::min(static_cast<std::size_t>(frames) * ch, avail);
            for (std::size_t f = 0; f < toWrite / ch; ++f) {
                for (int c = 0; c < recordChannels_; ++c)
                    recordBuffer_[pos++] = inputs[c] ? inputs[c][f] : 0.0f;
            }
            recordWritePos_.store(pos, std::memory_order_relaxed);
        } else {
            (void)inputs;
        }
    }

private:
    // ── Zone helpers ──────────────────────────────────────────────────────────

    void doZoneReorder(int from, int to)
    {
        std::lock_guard<std::mutex> lk(zoneMtx_);
        zonesInitialized_ = true;
        const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        if (!existing) return;
        const int n = static_cast<int>(existing->size());
        if (from < 0 || from >= n || to < 0 || to > n) return;
        if (to == from || to == from + 1) return;

        // Collect sorted ranges (by rangeLow) — these stay fixed on the keyboard.
        std::vector<std::pair<int,int>> ranges;
        ranges.reserve(static_cast<std::size_t>(n));
        for (const auto& z : *existing)
            ranges.push_back({z.rangeLow, z.rangeHigh});
        std::sort(ranges.begin(), ranges.end());

        // Move zone in the vector (PCM data + all zone metadata travels with it).
        auto newZones = std::make_shared<ZoneVec>(*existing);
        CoreSampleZone moved = (*newZones)[static_cast<std::size_t>(from)];
        newZones->erase(newZones->begin() + from);
        const int insertAt = (to > from) ? to - 1 : to;
        newZones->insert(newZones->begin() + insertAt, std::move(moved));

        // Redistribute sorted keyboard ranges to zones in their new display order.
        for (int i = 0; i < n; ++i) {
            (*newZones)[static_cast<std::size_t>(i)].rangeLow  = ranges[static_cast<std::size_t>(i)].first;
            (*newZones)[static_cast<std::size_t>(i)].rangeHigh = ranges[static_cast<std::size_t>(i)].second;
        }

        std::atomic_store_explicit(&zones_,
                                   std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                   std::memory_order_release);
        notifyZonesChanged(true);
    }

    std::string doSliceZone(int idx, int numSlices, int startNote)
    {
        std::lock_guard<std::mutex> lk(zoneMtx_);
        zonesInitialized_ = true;
        const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        if (!existing || idx < 0 || idx >= static_cast<int>(existing->size()))
            return "zone index out of range";
        const CoreSampleZone& src = (*existing)[static_cast<std::size_t>(idx)];
        if (src.data.empty()) return "zone has no audio data";
        const int totalFrames = static_cast<int>(src.data.size() / src.channelCount);
        // autoSpread: distribute slices evenly across C2–B5 (MIDI 36–83, 4 octaves)
        const bool autoSpread = (startNote < 0);
        std::vector<uint32_t> boundaries;
        if (numSlices == 0) {
            const auto onsets = downspout::campione::detectTransients(
                src.data, src.channelCount, src.sampleRate);
            if (onsets.empty()) return "no transients detected, specify num_slices explicitly";
            boundaries.push_back(0);
            for (uint32_t f : onsets) if (f > 0) boundaries.push_back(f);
            boundaries.push_back(static_cast<uint32_t>(totalFrames));
            numSlices = static_cast<int>(boundaries.size()) - 1;
        } else {
            boundaries.resize(static_cast<std::size_t>(numSlices + 1));
            for (int s = 0; s <= numSlices; ++s)
                boundaries[static_cast<std::size_t>(s)] =
                    static_cast<uint32_t>(static_cast<int64_t>(s) * totalFrames / numSlices);
        }
        auto nz = std::make_shared<ZoneVec>(*existing);
        nz->erase(nz->begin() + idx);
        for (int s = 0; s < numSlices; ++s) {
            const uint32_t f0 = boundaries[static_cast<std::size_t>(s)];
            const uint32_t f1 = boundaries[static_cast<std::size_t>(s + 1)];
            if (f1 <= f0) continue;
            CoreSampleZone slice;
            slice.channelCount  = src.channelCount;
            slice.sampleRate    = src.sampleRate;
            const std::size_t start = static_cast<std::size_t>(f0) * src.channelCount;
            const std::size_t end   = static_cast<std::size_t>(f1) * src.channelCount;
            slice.data.assign(src.data.begin() + static_cast<std::ptrdiff_t>(start),
                              src.data.begin() + static_cast<std::ptrdiff_t>(end));
            // Assign MIDI notes: spread evenly across C2–B5 (MIDI 36–83) or use explicit startNote
            if (autoSpread) {
                constexpr int kLow = 36, kHigh = 83, kSpan = kHigh - kLow + 1;
                if (numSlices <= kSpan) {
                    const int lo = kLow + s * kSpan / numSlices;
                    const int hi = (s == numSlices - 1)
                                   ? kHigh
                                   : kLow + (s + 1) * kSpan / numSlices - 1;
                    slice.rootNote = lo;
                    slice.rangeLow = lo;
                    slice.rangeHigh = hi;
                } else {
                    const int note = std::clamp(kLow + s, 0, 127);
                    slice.rootNote = note;
                    slice.rangeLow = note;
                    slice.rangeHigh = note;
                }
            } else {
                const int note = std::clamp(startNote + s, 0, 127);
                slice.rootNote = note;
                slice.rangeLow = note;
                slice.rangeHigh = note;
            }
            slice.attackMs     = src.attackMs;
            slice.decayMs      = src.decayMs;
            slice.sustainLevel = src.sustainLevel;
            slice.releaseMs    = src.releaseMs;
            slice.filterEnabled  = src.filterEnabled;
            slice.filterType     = src.filterType;
            slice.filterCutoffHz = src.filterCutoffHz;
            slice.filterQ        = src.filterQ;
            slice.octaveShift    = src.octaveShift;
            // Save slice to disk so the UI can show its waveform and it survives project reload
            {
                const std::string dir = recordingOutputDir_.empty()
                    ? defaultDataDir()
                    : recordingOutputDir_;
                ::mkdir(dir.c_str(), 0755);
                char fname[256];
                std::snprintf(fname, sizeof(fname), "%s/slice_%lld_%d.wav",
                              dir.c_str(), static_cast<long long>(std::time(nullptr)), s);
                if (downspout::campione::saveWavZone(slice, fname).empty())
                    slice.sourcePath = fname;
            }
            nz->insert(nz->begin() + idx + s, std::move(slice));
        }
        std::atomic_store_explicit(&zones_,
                                   std::shared_ptr<const ZoneVec>(std::move(nz)),
                                   std::memory_order_release);
        notifyZonesChanged(true);
        return {};
    }

    // Returns empty string on success, error message on failure.
    std::string appendZone(const std::string& path)
    {
        // Load WAV outside the lock (slow I/O).
        auto result = downspout::campione::loadWavZone(path);
        if (!result.error.empty()) return result.error;

        result.zone.sourcePath = path;

        if (!result.hasEmbeddedRootNote) {
            result.zone.rootNote = 60;
            const double hz = downspout::campione::detectFundamentalHz(
                result.zone.data, result.zone.channelCount, result.zone.sampleRate);
            const int detected = static_cast<int>(std::lround(downspout::campione::freqToMidi(hz)));
            if (detected >= 0 && detected <= 127)
                result.zone.rootNote = detected;
        }

        // Only auto-compute loop points if the smpl chunk didn't provide them.
        if (!result.zone.loopEnabled)
            downspout::campione::applyLoopPoints(result.zone, parameters_.crossfadeDurationMs);

        // Lock out restoreZones, then atomically claim ownership and append.
        std::lock_guard<std::mutex> lk(zoneMtx_);
        zonesInitialized_ = true;
        const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        auto newZones = std::make_shared<ZoneVec>(existing ? *existing : ZoneVec{});
        newZones->push_back(std::move(result.zone));
        std::atomic_store_explicit(&zones_,
                                   std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                   std::memory_order_release);
        return {};
    }

    void restoreZones(const std::string& text)
    {
        // Deserialize metadata before acquiring the lock (involves no zone writes).
        const auto metas = downspout::campione::deserializeZones(text);
        if (!metas.has_value()) return;

        // Under the lock: bail out if zones have been modified by user/MCP.
        // Re-assert current state to the UI so the host's stale setState call
        // (which DPF forwards unconditionally via sendStateSetToUI) is overridden.
        std::lock_guard<std::mutex> lk(zoneMtx_);
        if (zonesInitialized_) { notifyZonesChanged(); return; }

        // Empty host state means the host has no saved zones (fresh project).
        // Don't mark initialized — allow kStateKeyDataDir to load the auto-save.
        if (metas->empty()) return;

        auto newZones = std::make_shared<ZoneVec>();
        newZones->reserve(metas->size());

        for (const CoreSampleZone& meta : *metas)
        {
            if (meta.sourcePath.empty()) continue;
            auto result = downspout::campione::loadWavZone(meta.sourcePath);
            if (!result.error.empty()) continue;

            result.zone.rootNote        = meta.rootNote;
            result.zone.rangeLow        = meta.rangeLow;
            result.zone.rangeHigh       = meta.rangeHigh;
            result.zone.midiChannel     = meta.midiChannel;
            result.zone.loopEnabled     = meta.loopEnabled;
            result.zone.loopStart       = meta.loopStart;
            result.zone.loopEnd         = meta.loopEnd;
            result.zone.crossfadeFrames = meta.crossfadeFrames;
            result.zone.sourcePath      = meta.sourcePath;
            result.zone.attackMs        = meta.attackMs;
            result.zone.decayMs         = meta.decayMs;
            result.zone.sustainLevel    = meta.sustainLevel;
            result.zone.releaseMs       = meta.releaseMs;
            result.zone.filterEnabled   = meta.filterEnabled;
            result.zone.filterType      = meta.filterType;
            result.zone.filterCutoffHz  = meta.filterCutoffHz;
            result.zone.filterQ         = meta.filterQ;
            result.zone.octaveShift     = meta.octaveShift;
            newZones->push_back(std::move(result.zone));
        }

        std::atomic_store_explicit(&zones_,
                                   std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                   std::memory_order_release);
        // Mark initialized so the host re-sending state on UI reopen won't
        // overwrite in-memory zones (REAPER sends IEditController::setState
        // with the saved project state each time the plugin window opens).
        zonesInitialized_ = true;
        // Seed the UI bridge so uiIdle() sees the correct state on first poll,
        // even before any MCP call triggers notifyZonesChanged().
        notifyZonesChanged();
    }

    // Returns "" on success, or a human-readable error. Callers that surface
    // results to the user (MCP) must not report success without checking this.
    std::string finalizeRecording()
    {
        const std::size_t written = recordWritePos_.load(std::memory_order_acquire);
        if (written == 0)
            return "no audio captured; the host audio engine may not be running";

        CoreSampleZone zone;
        zone.data         = std::vector<float>(recordBuffer_.begin(),
                                               recordBuffer_.begin() + static_cast<std::ptrdiff_t>(written));
        zone.channelCount = recordChannels_;
        zone.sampleRate   = getSampleRate();

        const double hz = downspout::campione::detectFundamentalHz(
            zone.data, zone.channelCount, zone.sampleRate);
        const int detected = static_cast<int>(std::lround(downspout::campione::freqToMidi(hz)));
        zone.rootNote  = (detected >= 0 && detected <= 127) ? detected : 60;
        zone.rangeLow  = zone.rootNote;
        zone.rangeHigh = zone.rootNote;

        std::string dir = recordingOutputDir_.empty()
                          ? defaultDataDir()
                          : recordingOutputDir_;
        ::mkdir(dir.c_str(), 0755);

        char fname[256];
        std::snprintf(fname, sizeof(fname), "%s/rec_%lld.wav",
                      dir.c_str(), static_cast<long long>(std::time(nullptr)));
        zone.sourcePath = fname;

        const std::string saveErr = downspout::campione::saveWavZone(zone, fname);
        if (!saveErr.empty())
            return "could not write " + std::string(fname) + ": " + saveErr;

        downspout::campione::applyLoopPoints(zone, parameters_.crossfadeDurationMs);

        const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        auto newZones = std::make_shared<ZoneVec>(existing ? *existing : ZoneVec{});
        newZones->push_back(std::move(zone));
        std::atomic_store_explicit(&zones_,
                                   std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                   std::memory_order_release);
        notifyZonesChanged(true);
        return {};
    }

    // Notify host+UI of zone list change. Called directly from the MCP thread;
    // also sets pendingZoneUpdate_ so run() re-sends if the host drops the
    // off-thread call (e.g. some hosts only accept updateStateValue from the
    // audio thread). Both paths are harmlessly idempotent.
    void notifyZonesChanged(bool saveAutoSave = false)
    {
        const auto zp = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        const std::string s = zp ? downspout::campione::serializeZones(*zp) : std::string{};
        // Write to in-process bridge — UI polls this in uiIdle().
        {
            std::lock_guard<std::mutex> lk(downspout::campione::uiBridge().zonesMtx);
            downspout::campione::uiBridge().zonesData = s;
        }
        const uint32_t serial =
            downspout::campione::uiBridge().zonesSerial.fetch_add(1, std::memory_order_release) + 1;
        // Bump the zones-version parameter immediately so the host relays
        // parameterChanged() to the UI without waiting for run().  run() also
        // does this via pendingZoneUpdate_ as a belt-and-suspenders fallback.
        zonesVersion_.store(static_cast<int>(serial), std::memory_order_release);
        requestParameterValueChange(kParamZonesVersion, static_cast<float>(serial));
        // Also attempt the DPF path (no-op in VST3 but works in other formats).
        updateStateValue(kStateKeyZones, s.c_str());
        pendingZoneUpdate_.store(true, std::memory_order_release);
        // Persist zone list changes to disk so they survive host state failures.
        if (saveAutoSave) autoSavePatch();
    }

    // ── MCP-sourced parameter update (called from MCP thread) ─────────────────

    void mcpSetParam(float CoreParameters::* field, float value)
    {
        std::lock_guard<std::mutex> lk(mcpParamsMtx_);
        if (!hasMcpParams_) mcpPendingParams_ = parameters_;
        mcpPendingParams_.*field = value;
        hasMcpParams_ = true;
        pendingParamNotify_.store(true, std::memory_order_release);
    }

    // ── Wave editing (called from MCP thread, safe via atomic zone swap) ──────

    std::string mcpEditZone(int idx,
        const std::function<std::string(CoreSampleZone&)>& edit)
    {
        std::lock_guard<std::mutex> lk(zoneMtx_);
        zonesInitialized_ = true;
        const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        if (!existing || idx < 0 || idx >= static_cast<int>(existing->size()))
            return "zone index out of range";

        auto newZones = std::make_shared<ZoneVec>(*existing);
        auto& z = (*newZones)[static_cast<std::size_t>(idx)];

        const std::string err = edit(z);
        if (!err.empty()) return err;

        if (!z.sourcePath.empty()) {
            const std::string saveErr = downspout::campione::saveWavZone(z, z.sourcePath);
            if (!saveErr.empty()) return "save failed: " + saveErr;
        }

        std::atomic_store_explicit(&zones_,
                                   std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                   std::memory_order_release);
        notifyZonesChanged();
        return {};
    }

    // ── Patch save / load ────────────────────────────────────────────────────

    void autoSavePatch()
    {
        if (recordingOutputDir_.empty()) return;
        doSavePatch(recordingOutputDir_ + "/campione_autosave.ttl");
    }

    std::string doSavePatch(const std::string& path)
    {
        std::string savePath = path;
        if (savePath.empty()) {
            const std::string dir = recordingOutputDir_.empty()
                ? defaultDataDir()
                : recordingOutputDir_;
            ::mkdir(dir.c_str(), 0755);
            char fname[256];
            std::snprintf(fname, sizeof(fname), "%s/patch_%lld.ttl",
                          dir.c_str(), static_cast<long long>(std::time(nullptr)));
            savePath = fname;
        }

        const auto zonesPtr = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
        downspout::campione::PatchData pd;
        pd.label      = "Campione Patch";
        pd.parameters = parameters_;
        if (zonesPtr) {
            for (const auto& z : *zonesPtr) {
                downspout::campione::SampleZone meta;
                meta.sourcePath    = z.sourcePath;
                meta.rootNote      = z.rootNote;
                meta.rangeLow      = z.rangeLow;
                meta.rangeHigh     = z.rangeHigh;
                meta.midiChannel   = z.midiChannel;
                meta.loopEnabled   = z.loopEnabled;
                meta.loopStart     = z.loopStart;
                meta.loopEnd       = z.loopEnd;
                meta.attackMs      = z.attackMs;
                meta.decayMs       = z.decayMs;
                meta.sustainLevel  = z.sustainLevel;
                meta.releaseMs     = z.releaseMs;
                meta.filterEnabled = z.filterEnabled;
                meta.filterType    = z.filterType;
                meta.filterCutoffHz = z.filterCutoffHz;
                meta.filterQ       = z.filterQ;
                meta.pan           = z.pan;
                meta.octaveShift   = z.octaveShift;
                pd.zones.push_back(std::move(meta));
            }
        }
        return downspout::campione::savePatch(pd, savePath);
    }

    std::string doLoadPatch(const std::string& path)
    {
        auto [pd, err] = downspout::campione::loadPatch(path);
        if (!pd.has_value()) return err;

        // Queue parameter update for audio thread
        {
            std::lock_guard<std::mutex> lk(mcpParamsMtx_);
            mcpPendingParams_ = pd->parameters;
            hasMcpParams_     = true;
            pendingParamNotify_.store(true, std::memory_order_release);
        }

        // Load zone WAV files
        auto newZones = std::make_shared<ZoneVec>();
        newZones->reserve(pd->zones.size());
        for (const auto& meta : pd->zones) {
            if (meta.sourcePath.empty()) continue;
            auto result = downspout::campione::loadWavZone(meta.sourcePath);
            if (!result.error.empty()) continue;
            result.zone.rootNote      = meta.rootNote;
            result.zone.rangeLow      = meta.rangeLow;
            result.zone.rangeHigh     = meta.rangeHigh;
            result.zone.midiChannel   = meta.midiChannel;
            result.zone.loopEnabled   = meta.loopEnabled;
            result.zone.loopStart     = meta.loopStart;
            result.zone.loopEnd       = meta.loopEnd;
            result.zone.crossfadeFrames = meta.crossfadeFrames;
            result.zone.sourcePath    = meta.sourcePath;
            result.zone.attackMs      = meta.attackMs;
            result.zone.decayMs       = meta.decayMs;
            result.zone.sustainLevel  = meta.sustainLevel;
            result.zone.releaseMs     = meta.releaseMs;
            result.zone.filterEnabled = meta.filterEnabled;
            result.zone.filterType    = meta.filterType;
            result.zone.filterCutoffHz = meta.filterCutoffHz;
            result.zone.filterQ       = meta.filterQ;
            result.zone.pan           = meta.pan;
            result.zone.octaveShift   = meta.octaveShift;
            newZones->push_back(std::move(result.zone));
        }

        {
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::move(newZones)),
                                       std::memory_order_release);
        }
        lastPatchPath_ = path;
        notifyZonesChanged(true);
        return {};
    }

    // ── MCP API factory ───────────────────────────────────────────────────────

    downspout::campione::CampioneMcpServer::Api buildMcpApi()
    {
        using namespace downspout::campione;
        CampioneMcpServer::Api api;

        api.getZonesJson = [this]() -> std::string {
            // Querying zones means MCP is active — lock out host setState from this point.
            { std::lock_guard<std::mutex> lk(zoneMtx_); zonesInitialized_ = true; }
            const auto z = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            return z ? zonesAsJson(*z) : "{\"zones\":[]}";
        };

        api.getParametersJson = [this]() -> std::string {
            return paramsAsJson(parameters_);
        };

        api.getPort = [this]() -> int {
            return mcp_ ? mcp_->port() : 0;
        };

        api.setMasterVolume = [this](float v) {
            mcpSetParam(&CoreParameters::masterVolume, v);
        };
        api.setMidiChannel = [this](float ch) {
            mcpSetParam(&CoreParameters::midiChannel, ch);
        };
        api.setCrossfadeMs = [this](float ms) {
            mcpSetParam(&CoreParameters::crossfadeDurationMs, ms);
        };
        api.setPitchBendRange = [this](float sem) {
            mcpSetParam(&CoreParameters::pitchBendRange, sem);
        };

        api.startRecording = [this]() -> std::string {
            if (recording_.load(std::memory_order_acquire))
                return "already recording";
            recordChannels_ = DISTRHO_PLUGIN_NUM_INPUTS;
            recordBuffer_.assign(kMaxRecordSamples, 0.0f);
            recordWritePos_.store(0, std::memory_order_release);
            recording_.store(true, std::memory_order_release);
            return {};
        };

        api.stopRecording = [this]() -> std::string {
            if (!recording_.load(std::memory_order_acquire))
                return "not recording";
            recording_.store(false, std::memory_order_release);
            const std::string err = finalizeRecording();
            if (!err.empty()) return "recording stopped, no zone added: " + err;
            return {};
        };

        api.loadZone = [this](const std::string& path) -> std::string {
            const std::string err = appendZone(path);  // single load, errors propagated
            if (!err.empty()) return err;
            notifyZonesChanged(true);
            return {};
        };

        api.importWavetable = [this](const std::string& path) -> std::string {
            const std::string dir = recordingOutputDir_.empty() ? defaultDataDir() : recordingOutputDir_;
            ::mkdir(dir.c_str(), 0755);
            auto result = downspout::campione::importWavetableZone(path, dir);
            if (!result.error.empty()) return result.error;
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            auto nz = std::make_shared<ZoneVec>(existing ? *existing : ZoneVec{});
            nz->push_back(std::move(result.zone));
            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::move(nz)),
                                       std::memory_order_release);
            notifyZonesChanged(true);
            return {};
        };

        api.reorderZone = [this](int from, int to) -> std::string {
            {
                const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
                const int n = existing ? static_cast<int>(existing->size()) : 0;
                if (from < 0 || from >= n || to < 0 || to > n)
                    return "index out of range";
            }
            doZoneReorder(from, to);
            return {};
        };

        api.previewZone = [this](int idx) {
            pendingPreviewZone_.store(idx, std::memory_order_release);
        };

        api.removeZone = [this](int idx) -> std::string {
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            if (!existing || idx < 0 || idx >= static_cast<int>(existing->size()))
                return "zone index out of range";
            auto nz = std::make_shared<ZoneVec>(*existing);
            nz->erase(nz->begin() + idx);
            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::move(nz)),
                                       std::memory_order_release);
            notifyZonesChanged(true);
            return {};
        };

        api.updateZone = [this](int idx, int root, int lo, int hi, bool loop,
                                 uint32_t lStart, uint32_t lEnd, int octaveShift) -> std::string {
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            if (!existing || idx < 0 || idx >= static_cast<int>(existing->size()))
                return "zone index out of range";
            auto nz = std::make_shared<ZoneVec>(*existing);
            auto& z = (*nz)[static_cast<std::size_t>(idx)];
            z.rootNote    = root;
            z.rangeLow    = lo;
            z.rangeHigh   = hi;
            z.loopEnabled = loop;
            z.octaveShift = std::clamp(octaveShift, -6, 6);
            if (lEnd > lStart) {
                z.loopStart = lStart;
                z.loopEnd   = lEnd;
                const uint32_t loopLen = lEnd - lStart;
                z.crossfadeFrames = static_cast<uint32_t>(
                    (parameters_.crossfadeDurationMs / 1000.0f) * z.sampleRate);
                if (z.crossfadeFrames > loopLen / 2) z.crossfadeFrames = loopLen / 2;
            } else {
                applyLoopPoints(z, parameters_.crossfadeDurationMs);
            }
            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::move(nz)),
                                       std::memory_order_release);
            notifyZonesChanged();
            return {};
        };

        api.updateZoneDsp = [this](int idx,
                                    float attackMs, float decayMs, float sustainLevel, float releaseMs,
                                    int filterEnabled, int filterType, float filterCutoff, float filterQ,
                                    float pan, int muted)
                                    -> std::string {
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            if (!existing || idx < 0 || idx >= static_cast<int>(existing->size()))
                return "zone index out of range";
            auto nz = std::make_shared<ZoneVec>(*existing);
            auto& z = (*nz)[static_cast<std::size_t>(idx)];
            // Negative = preserve existing value (not provided by caller)
            if (attackMs      >= 0.0f) z.attackMs      = attackMs;
            if (decayMs       >= 0.0f) z.decayMs       = decayMs;
            if (sustainLevel  >= 0.0f) z.sustainLevel  = std::clamp(sustainLevel, 0.0f, 1.0f);
            if (releaseMs     >= 0.0f) z.releaseMs     = releaseMs;
            if (filterEnabled >= 0)    z.filterEnabled = filterEnabled != 0;
            if (filterType    >= 0)    z.filterType    = filterType;
            if (filterCutoff  >= 0.0f) z.filterCutoffHz = filterCutoff;
            if (filterQ       >= 0.0f) z.filterQ       = filterQ;
            if (pan           >= -1.0f && pan <= 1.0f) z.pan = pan;
            if (muted         >= 0)    z.muted         = muted != 0;
            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::move(nz)),
                                       std::memory_order_release);
            notifyZonesChanged();
            return {};
        };

        api.mapDrum = [this]() -> std::string {
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            if (!existing || existing->empty()) return "Error: no zones loaded";
            auto nz = std::make_shared<ZoneVec>(*existing);

            const auto assignments = downspout::campione::assignDrumNotes(*existing);

            std::string summary;
            int assignedCount = 0;
            for (int i = 0; i < static_cast<int>(assignments.size()); ++i) {
                const auto& a = assignments[static_cast<std::size_t>(i)];
                if (a.gmNote >= 0) {
                    (*nz)[static_cast<std::size_t>(i)].rootNote  = a.gmNote;
                    (*nz)[static_cast<std::size_t>(i)].rangeLow  = a.gmNote;
                    (*nz)[static_cast<std::size_t>(i)].rangeHigh = a.gmNote;
                    ++assignedCount;
                    const auto& path = (*existing)[static_cast<std::size_t>(i)].sourcePath;
                    const auto sep  = path.find_last_of("/\\");
                    const auto base = (sep == std::string::npos) ? path : path.substr(sep + 1);
                    char buf[160];
                    std::snprintf(buf, sizeof(buf), "\n  %s → %s (note %d)",
                                  base.c_str(), a.evidence.c_str(), a.gmNote);
                    summary += buf;
                }
            }

            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::move(nz)),
                                       std::memory_order_release);
            notifyZonesChanged(true);

            char header[80];
            std::snprintf(header, sizeof(header), "%d/%d zones assigned to GM drum notes:",
                          assignedCount, static_cast<int>(assignments.size()));
            return std::string(header) + summary;
        };

        api.mapSpread = [this]() -> std::string {
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            const auto existing = std::atomic_load_explicit(&zones_, std::memory_order_acquire);
            if (!existing || existing->empty()) return "no zones loaded";
            auto nz = std::make_shared<ZoneVec>(*existing);
            constexpr int kLow = 36, kRange = 48;
            const int n = static_cast<int>(nz->size());
            for (int i = 0; i < n; ++i) {
                const int lo   = kLow + i * kRange / n;
                const int hi   = kLow + (i + 1) * kRange / n - 1;
                (*nz)[i].rootNote  = (lo + hi) / 2;
                (*nz)[i].rangeLow  = lo;
                (*nz)[i].rangeHigh = hi;
            }
            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::move(nz)),
                                       std::memory_order_release);
            notifyZonesChanged(true);
            return {};
        };

        api.sliceZone = [this](int idx, int numSlices, int startNote) -> std::string {
            return doSliceZone(idx, numSlices, startNote);
        };

        api.normalizeZone = [this](int idx) -> std::string {
            return mcpEditZone(idx, [](CoreSampleZone& z) {
                return normalizeZone(z);
            });
        };

        api.trimZone = [this](int idx, float thresholdDb) -> std::string {
            return mcpEditZone(idx, [thresholdDb](CoreSampleZone& z) -> std::string {
                const float lin = std::pow(10.0f, thresholdDb / 20.0f);
                trimZone(z, lin);
                return {};
            });
        };

        api.fadeZone = [this](int idx, float inMs, float outMs) -> std::string {
            return mcpEditZone(idx, [inMs, outMs](CoreSampleZone& z) -> std::string {
                const auto inF  = static_cast<uint32_t>((inMs  / 1000.0f) * z.sampleRate);
                const auto outF = static_cast<uint32_t>((outMs / 1000.0f) * z.sampleRate);
                fadeZone(z, inF, outF);
                return {};
            });
        };

        api.reverseZone = [this](int idx) -> std::string {
            return mcpEditZone(idx, [](CoreSampleZone& z) -> std::string {
                reverseZone(z);
                return {};
            });
        };

        api.clearZones = [this]() {
            std::lock_guard<std::mutex> lk(zoneMtx_);
            zonesInitialized_ = true;
            std::atomic_store_explicit(&zones_,
                                       std::shared_ptr<const ZoneVec>(std::make_shared<ZoneVec>()),
                                       std::memory_order_release);
            notifyZonesChanged(true);
        };

        api.refreshUi = [this]() {
            notifyZonesChanged();
            // Also push current params to bridge.
            {
                std::lock_guard<std::mutex> lk(downspout::campione::uiBridge().paramsMtx);
                auto& b = downspout::campione::uiBridge();
                b.masterVolume   = parameters_.masterVolume;
                b.midiChannel    = parameters_.midiChannel;
                b.crossfadeMs    = parameters_.crossfadeDurationMs;
                b.pitchBendRange = parameters_.pitchBendRange;
            }
            downspout::campione::uiBridge().paramsSerial.fetch_add(1, std::memory_order_release);
        };

        api.getRecordingStatus = [this]() -> std::string {
            const bool rec    = recording_.load(std::memory_order_acquire);
            const std::size_t frames = recordWritePos_.load(std::memory_order_acquire)
                                       / std::max(1, recordChannels_);
            const double secs = getSampleRate() > 0
                                 ? static_cast<double>(frames) / getSampleRate() : 0.0;
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "{\"recording\":%s,\"frames_captured\":%zu,\"seconds_captured\":%.3f}",
                          rec ? "true" : "false", frames, secs);
            return buf;
        };

        api.setRecordingDir = [this](const std::string& path) -> std::string {
            struct stat st{};
            if (::stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
                return "directory does not exist: " + path;
            recordingOutputDir_ = path;
            return {};
        };

        api.savePatch = [this](const std::string& path) -> std::string {
            return doSavePatch(path);
        };

        api.loadPatch = [this](const std::string& path) -> std::string {
            return doLoadPatch(path);
        };

        return api;
    }

    // ── State ─────────────────────────────────────────────────────────────────

    static constexpr std::size_t kMaxRecordSamples = 48000u * 2u * 60u * 5u;

    CoreParameters  parameters_ {};
    CoreEngineState engineState_ {};
    std::shared_ptr<const ZoneVec> zones_ {};
    const ZoneVec                  emptyZones_ {};

    std::string              lastPatchPath_;
    std::atomic<bool>        pendingZoneUpdate_   {false};
    std::atomic<bool>        pendingParamNotify_  {false};
    // -2 = nothing pending; -1 = stop preview; >=0 = play zone at that index
    std::atomic<int>         pendingPreviewZone_  {-2};
    int                      previewNote_         {-1};  // audio-thread only
    std::atomic<int>         zonesVersion_       {0};
    std::mutex               zoneMtx_;           // guards zonesInitialized_ + zone writes
    bool                     zonesInitialized_  {false};
    std::atomic<bool>        recording_         {false};
    std::atomic<std::size_t> recordWritePos_    {0};
    std::vector<float>       recordBuffer_;
    int                      recordChannels_ = 2;

    // MCP-sourced parameter queue (mutex + pending struct; try_lock in run())
    std::mutex       mcpParamsMtx_;
    CoreParameters   mcpPendingParams_ {};
    bool             hasMcpParams_ = false;

    std::string              recordingOutputDir_;

    std::unique_ptr<downspout::campione::CampioneMcpServer> mcp_;
    bool                     mcpEnabled_ {true};  // default on; server starts in activate()
    bool                     mcpStarted_ {false}; // true after first activate()

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CampionePlugin)
};

Plugin* createPlugin()
{
    return new CampionePlugin();
}

END_NAMESPACE_DISTRHO
