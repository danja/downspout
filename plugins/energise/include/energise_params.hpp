#pragma once

#include "energise_core.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace downspout::energise {

// One stable parameter table shared by the core, DPF wrapper, UI and profile.
// Writable controls and request counters come first; read-only status outputs,
// the waveform shadow and event-list shadow follow them.

enum class ParamId : std::uint32_t {
    captureBars = 0,
    grid,
    quantiseStrength,
    ghostStrength,
    swing,
    humanise,
    driftAllowance,
    transientLevel,
    bodyLevel,
    residualLevel,
    saturation,
    variation,
    outputTrim,
    offsetSelectedMs,
    gainSelected,
    muteMask,
    soloMask,
    eventPage,
    seed,
    audioOut,
    midiOut,
    bbtLoop,

    // Event edits use ordinary values plus a request counter. Event shadow
    // parameters below are read-only and host polling is not a reliable edit path.
    selectedEvent,
    editClass,
    editTime,
    editVelocity,
    editGhost,

    captureRequest,
    analyseRequest,
    renderRequest,
    abSwap,
    undoRequest,
    redoRequest,
    exportMidiRequest,
    demoRequest,
    eventEditRequest,

    status,
    progress,
    eventCount,
    selectedClass,
    selectedConfidence,
    renderReady,
    abSide,
    tempoBpm,
    bbtValid,

    waveStart,
    eventStart = 93,
    count = 165,
};

inline constexpr std::size_t kWaveformPoints = 48;
inline constexpr std::size_t kVisibleEvents = 12;
inline constexpr std::size_t kEventFields = 6;
inline constexpr std::size_t kInputParameterCount = 36;
inline constexpr std::size_t kRequestCount = 9;
inline constexpr std::size_t kStatusParameterCount = 9;
inline constexpr std::size_t kParameterCount = static_cast<std::size_t>(ParamId::count);
inline constexpr float kMaxRequestCount = 1048576.0f;

enum class AudioOutMode : std::uint8_t {
    Off = 0,
    Pass,
    Render,
};

inline constexpr std::array<const char*, 3> kAudioOutNames = {{
    "Off", "Pass", "Render",
}};

constexpr AudioOutMode audioOutMode(const float value) noexcept
{
    const int index = static_cast<int>(value + 0.5f);
    if (index == 1)
        return AudioOutMode::Pass;
    if (index == 2)
        return AudioOutMode::Render;
    return AudioOutMode::Off;
}

constexpr bool midiOutEnabled(const float value) noexcept { return value >= 0.5f; }
constexpr bool bbtLoopEnabled(const float value) noexcept { return value >= 0.5f; }

static_assert(kParameterCount == 165, "Processor::params_ storage must match this table");
static_assert(static_cast<std::size_t>(ParamId::waveStart) + kWaveformPoints == static_cast<std::size_t>(ParamId::eventStart));
static_assert(static_cast<std::size_t>(ParamId::eventStart) + kVisibleEvents * kEventFields == kParameterCount);

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
};

inline constexpr std::array<const char*, 4> kGridNames = {{
    "1/8", "1/16", "1/32", "1/16T",
}};

inline constexpr std::array<int, 4> kGridDivisions = {{2, 4, 8, 6}};

[[nodiscard]] inline int gridDivisions(const int index) noexcept
{
    const int clamped = index < 0 ? 0 : (index >= static_cast<int>(kGridDivisions.size()) ? 3 : index);
    return kGridDivisions[static_cast<std::size_t>(clamped)];
}

[[nodiscard]] inline std::uint16_t classBit(const DrumClass cls) noexcept
{
    return static_cast<std::uint16_t>(1u << static_cast<unsigned>(cls));
}

namespace detail {

// Generated symbol/name strings need stable storage for the const char* fields.
// A deque keeps earlier c_str() pointers valid as new strings are added.
struct SpecBuilder {
    std::deque<std::string> strings {};

    [[nodiscard]] const char* intern(const std::string& text)
    {
        strings.push_back(text);
        return strings.back().c_str();
    }
};

inline const std::array<ParamSpec, kParameterCount>& makeParameterSpecs()
{
    static const std::array<ParamSpec, kParameterCount> specs = [] {
        SpecBuilder builder;
        std::array<ParamSpec, kParameterCount> specs {};
    specs[static_cast<std::size_t>(ParamId::captureBars)] = {"capture_bars", "Capture Bars", "bars", 1.0f, 4.0f, 2.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::grid)] = {"grid", "Grid", "", 0.0f, 3.0f, 1.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::quantiseStrength)] = {"quantise_strength", "Quantise", "%", 0.0f, 1.0f, 0.70f, false, false};
    specs[static_cast<std::size_t>(ParamId::ghostStrength)] = {"ghost_strength", "Ghosts", "%", 0.0f, 1.0f, 0.35f, false, false};
    specs[static_cast<std::size_t>(ParamId::swing)] = {"swing", "Swing", "%", 0.0f, 1.0f, 0.0f, false, false};
    specs[static_cast<std::size_t>(ParamId::humanise)] = {"humanise", "Humanise", "%", 0.0f, 1.0f, 0.0f, false, false};
    specs[static_cast<std::size_t>(ParamId::driftAllowance)] = {"drift_allowance", "Drift", "%", 0.0f, 1.0f, 0.25f, false, false};
    specs[static_cast<std::size_t>(ParamId::transientLevel)] = {"transient_level", "Transient", "%", 0.0f, 1.0f, 0.60f, false, false};
    specs[static_cast<std::size_t>(ParamId::bodyLevel)] = {"body_level", "Body", "%", 0.0f, 1.0f, 0.80f, false, false};
    specs[static_cast<std::size_t>(ParamId::residualLevel)] = {"residual_level", "Residual", "%", 0.0f, 1.0f, 0.35f, false, false};
    specs[static_cast<std::size_t>(ParamId::saturation)] = {"saturation", "Saturation", "%", 0.0f, 1.0f, 0.25f, false, false};
    specs[static_cast<std::size_t>(ParamId::variation)] = {"variation", "Variation", "%", 0.0f, 1.0f, 0.20f, false, false};
    specs[static_cast<std::size_t>(ParamId::outputTrim)] = {"output_trim", "Output", "%", 0.0f, 1.0f, 0.70f, false, false};
    specs[static_cast<std::size_t>(ParamId::offsetSelectedMs)] = {"offset_selected_ms", "Class Offset", "ms", -60.0f, 60.0f, 0.0f, false, false};
    specs[static_cast<std::size_t>(ParamId::gainSelected)] = {"gain_selected", "Class Gain", "x", 0.0f, 2.0f, 1.0f, false, false};
    specs[static_cast<std::size_t>(ParamId::muteMask)] = {"mute_mask", "Mutes", "", 0.0f, 8191.0f, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::soloMask)] = {"solo_mask", "Solos", "", 0.0f, 8191.0f, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::eventPage)] = {"event_page", "Event Page", "", 0.0f, 42.0f, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::seed)] = {"seed", "Seed", "", 1.0f, 9999.0f, 1.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::audioOut)] = {"audio_out", "Audio Out", "", 0.0f, 2.0f, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::midiOut)] = {"midi_out", "MIDI Out", "", 0.0f, 1.0f, 1.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::bbtLoop)] = {"bbt_loop", "BBT Loop", "", 0.0f, 1.0f, 1.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::selectedEvent)] = {"selected_event", "Selected Event", "", 0.0f, 511.0f, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::editClass)] = {"edit_class", "Edit Class", "", 0.0f, 12.0f, 12.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::editTime)] = {"edit_time", "Edit Time", "s", 0.0f, 8.0f, 0.0f, false, false};
    specs[static_cast<std::size_t>(ParamId::editVelocity)] = {"edit_velocity", "Edit Velocity", "%", 0.0f, 1.0f, 0.0f, false, false};
    specs[static_cast<std::size_t>(ParamId::editGhost)] = {"edit_ghost", "Edit Ghost", "", 0.0f, 1.0f, 0.0f, true, false};

    specs[static_cast<std::size_t>(ParamId::captureRequest)] = {"capture_request", "Capture", "", 0.0f, kMaxRequestCount, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::analyseRequest)] = {"analyse_request", "Analyse", "", 0.0f, kMaxRequestCount, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::renderRequest)] = {"render_request", "Render", "", 0.0f, kMaxRequestCount, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::abSwap)] = {"ab_swap", "A/B", "", 0.0f, kMaxRequestCount, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::undoRequest)] = {"undo_request", "Undo", "", 0.0f, kMaxRequestCount, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::redoRequest)] = {"redo_request", "Redo", "", 0.0f, kMaxRequestCount, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::exportMidiRequest)] = {"export_midi_request", "Export MIDI", "", 0.0f, kMaxRequestCount, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::demoRequest)] = {"demo_request", "Demo Break", "", 0.0f, kMaxRequestCount, 0.0f, true, false};
    specs[static_cast<std::size_t>(ParamId::eventEditRequest)] = {"event_edit_request", "Edit Event", "", 0.0f, kMaxRequestCount, 0.0f, true, false};

    specs[static_cast<std::size_t>(ParamId::status)] = {"status", "Status", "", 0.0f, 5.0f, 0.0f, true, true};
    specs[static_cast<std::size_t>(ParamId::progress)] = {"progress", "Progress", "%", 0.0f, 1.0f, 0.0f, false, true};
    specs[static_cast<std::size_t>(ParamId::eventCount)] = {"event_count", "Events", "", 0.0f, 512.0f, 0.0f, true, true};
    specs[static_cast<std::size_t>(ParamId::selectedClass)] = {"selected_class", "Selected Class", "", 0.0f, 12.0f, 12.0f, true, true};
    specs[static_cast<std::size_t>(ParamId::selectedConfidence)] = {"selected_confidence", "Confidence", "%", 0.0f, 1.0f, 0.0f, false, true};
    specs[static_cast<std::size_t>(ParamId::renderReady)] = {"render_ready", "Render Ready", "", 0.0f, 1.0f, 0.0f, true, true};
    specs[static_cast<std::size_t>(ParamId::abSide)] = {"ab_side", "A/B Side", "", 0.0f, 1.0f, 0.0f, true, true};
    specs[static_cast<std::size_t>(ParamId::tempoBpm)] = {"tempo_bpm", "Tempo", "bpm", 0.0f, 250.0f, 120.0f, false, true};
    specs[static_cast<std::size_t>(ParamId::bbtValid)] = {"bbt_valid", "BBT Valid", "", 0.0f, 1.0f, 0.0f, true, true};

    for (std::size_t i = 0; i < kWaveformPoints; ++i) {
        char symbol[16];
        char name[24];
        std::snprintf(symbol, sizeof(symbol), "wave_%zu", i);
        std::snprintf(name, sizeof(name), "Wave %zu", i);
        specs[static_cast<std::size_t>(ParamId::waveStart) + i] = {
            builder.intern(symbol), builder.intern(name), "", -1.0f, 1.0f, 0.0f, false, true,
        };
    }

    static constexpr std::array<const char*, kEventFields> fieldNames = {{
        "time", "class", "velocity", "confidence", "quant", "ghost",
    }};
    static constexpr std::array<float, kEventFields> fieldMax = {{
        8.0f, 12.0f, 1.0f, 1.0f, 8.0f, 1.0f,
    }};
    for (std::size_t slot = 0; slot < kVisibleEvents; ++slot) {
        for (std::size_t field = 0; field < kEventFields; ++field) {
            char symbol[32];
            char name[40];
            std::snprintf(symbol, sizeof(symbol), "ev%zu_%s", slot, fieldNames[field]);
            std::snprintf(name, sizeof(name), "Event %zu %s", slot + 1, fieldNames[field]);
            specs[static_cast<std::size_t>(ParamId::eventStart) + slot * kEventFields + field] = {
                builder.intern(symbol), builder.intern(name), "", 0.0f, fieldMax[field], 0.0f, true, true,
            };
        }
    }
        return specs;
    }();
    return specs;
}

}  // namespace detail

inline const std::array<ParamSpec, kParameterCount>& kParameterSpecs = detail::makeParameterSpecs();

[[nodiscard]] inline const ParamSpec& parameterSpec(const ParamId id) noexcept
{
    return kParameterSpecs[static_cast<std::size_t>(id)];
}

}  // namespace downspout::energise
