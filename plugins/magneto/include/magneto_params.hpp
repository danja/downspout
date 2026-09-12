#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::magneto {

// ── Parameters ─────────────────────────────────────────────────────────────
//
// 27 host-writable parameters followed by 2 read-only status parameters the UI
// uses to show the processor's live state (smoothed RPM and the backfire lamp).
//
// Parameters 0–17 come from the SIVE'15 engine model (Baldan et al.); 18–26 are
// plugin-level additions for host integration. See docs/design.md.

enum class ParamId : std::uint32_t {
    // Engine
    cylinders = 0,
    displacement,
    compression,
    ignition,
    asymmetry,
    blockGain,
    // Intake
    intakeLen,
    intakeGain,
    turbulence,
    // Exhaust
    extractorLen,
    pipeLen,
    mufflerLen,
    mufflerAction,
    outletLen,
    outletGain,
    backfire,
    // Drive
    rpm,
    throttle,
    rpmSource,
    syncRatio,
    idleRpm,
    inertia,
    seed,
    midiCh,
    // Output
    listen,
    width,
    level,
    // Read-only status
    outRpm,
    outBackfire,
};

inline constexpr std::size_t kParameterCount = 29;
inline constexpr std::size_t kInputParameterCount = 27;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;  // empty string when unitless
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"cylinders", "Cylinders", "", 1.0f, 12.0f, 4.0f, true, false},
    {"displacement", "Displacement", "cc", 100.0f, 1200.0f, 500.0f, false, false},
    {"compression", "Compression", ":1", 6.0f, 14.0f, 10.0f, false, false},
    {"ignition", "Ignition", "", 0.02f, 1.0f, 0.15f, false, false},
    {"asymmetry", "Growl", "", 0.0f, 1.0f, 0.12f, false, false},
    {"block_gain", "Block", "", 0.0f, 1.0f, 0.35f, false, false},

    {"intake_len", "Intake Len", "m", 0.05f, 1.50f, 0.35f, false, false},
    {"intake_gain", "Intake", "", 0.0f, 1.0f, 0.50f, false, false},
    {"turbulence", "Turbulence", "", 0.0f, 1.0f, 0.60f, false, false},

    {"extractor_len", "Extractor", "m", 0.10f, 1.50f, 0.60f, false, false},
    {"pipe_len", "Pipe", "m", 0.20f, 4.00f, 1.80f, false, false},
    {"muffler_len", "Muffler", "m", 0.10f, 1.50f, 0.50f, false, false},
    {"muffler_action", "Silencing", "", 0.0f, 1.0f, 0.60f, false, false},
    {"outlet_len", "Outlet", "m", 0.05f, 1.00f, 0.25f, false, false},
    {"outlet_gain", "Exhaust", "", 0.0f, 1.0f, 0.80f, false, false},
    {"backfire", "Backfire", "", 0.0f, 1.0f, 0.20f, false, false},

    {"rpm", "RPM", "rpm", 400.0f, 9000.0f, 850.0f, false, false},
    {"throttle", "Throttle", "", 0.0f, 1.0f, 0.0f, false, false},
    {"rpm_source", "Speed Src", "", 0.0f, 1.0f, 0.0f, true, false},
    {"sync_ratio", "Sync Ratio", "", 0.0f, 7.0f, 3.0f, true, false},
    {"idle_rpm", "Idle", "rpm", 400.0f, 1500.0f, 800.0f, false, false},
    {"inertia", "Inertia", "ms", 10.0f, 3000.0f, 400.0f, false, false},
    {"seed", "Seed", "", 1.0f, 9999.0f, 1.0f, true, false},
    {"midi_ch", "MIDI Ctl", "", 0.0f, 17.0f, 17.0f, true, false},

    {"listen", "Listen", "", 0.0f, 3.0f, 0.0f, true, false},
    {"width", "Width", "", 0.0f, 1.0f, 0.60f, false, false},
    {"level", "Level", "", 0.0f, 1.0f, 0.75f, false, false},

    {"out_rpm", "Engine RPM", "rpm", 0.0f, 9000.0f, 0.0f, false, true},
    {"out_backfire", "Backfire Lamp", "", 0.0f, 1.0f, 0.0f, false, true},
}};

// ── Enumerated value tables ────────────────────────────────────────────────

inline constexpr std::array<const char*, 2> kRpmSourceNames = {{
    "Manual",
    "Host Sync",
}};

// Engine cycles per host beat. One engine cycle is two crankshaft revolutions,
// so targetRpm = 120 * ratio * bpm / 60 = 2 * bpm * ratio.
inline constexpr std::array<float, 8> kSyncRatioValues = {{
    1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f,
}};

inline constexpr std::array<const char*, 8> kSyncRatioNames = {{
    "1 /beat", "2 /beat", "3 /beat", "4 /beat",
    "6 /beat", "8 /beat", "12 /beat", "16 /beat",
}};

inline constexpr std::array<const char*, 4> kListenNames = {{
    "Cabin",
    "Front",
    "Rear",
    "Exterior",
}};

// 0 = Off, 1–16 = that MIDI channel only, 17 = all channels.
inline constexpr std::array<const char*, 18> kMidiChannelNames = {{
    "Off", "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8",
    "Ch 9", "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16", "All",
}};

// ── Fixed MIDI controller map ──────────────────────────────────────────────
//
// Downspout convention: incoming CC writes through to the host parameter so
// automation lanes and the panel stay in agreement.

inline constexpr std::uint8_t kCcThrottle = 1;    // mod wheel
inline constexpr std::uint8_t kCcRpm = 2;         // breath
inline constexpr std::uint8_t kCcLevel = 7;       // channel volume
inline constexpr std::uint8_t kCcThrottleAlt = 11;  // expression

}  // namespace downspout::magneto
