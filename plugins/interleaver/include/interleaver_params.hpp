#pragma once

#include <array>
#include <cstdint>

namespace downspout::interleaver {

// Parameter indices are stable: never reorder, only append. The last two are read-only status outputs.
enum ParameterIndex : std::uint32_t {
    kMode = 0,      // 0 Time, 1 Spectral
    kBalance,       // A share of each cycle, percent
    kSplit,         // stereo split: shifts the right channel's pattern, percent of half a cycle
    kSlice,         // free-running slice length (ms), Time mode
    kSync,          // 0 Free, 1 Track A, 2 Track B
    kPeriods,       // pitch periods per slice when tracking
    kCrossfade,     // edge softness, percent of the shorter segment
    kShape,         // 0 Hard, 1 Linear, 2 Cosine
    kJitter,        // random cycle-length variation, percent
    kScheme,        // Spectral: 0 Bins, 1 Mag/Phase, 2 Loudest
    kBinWidth,      // Spectral: bins per segment
    kDrift,         // Spectral: mask rotation, bins per second
    kOutput,        // dB
    kDetectedHz,    // status output: tracked fundamental (0 = none)
    kCycleMs,       // status output: cycle length in use (A segment + B segment)
    kParameterCount
};

constexpr std::uint32_t kFirstOutputParameter = kDetectedHz;

enum Mode { kModeTime = 0, kModeSpectral = 1, kModeCount };
enum Sync { kSyncFree = 0, kSyncTrackA = 1, kSyncTrackB = 2, kSyncCount };
enum Shape { kShapeHard = 0, kShapeLinear = 1, kShapeCosine = 2, kShapeCount };
enum Scheme { kSchemeBins = 0, kSchemeMagPhase = 1, kSchemeLoudest = 2, kSchemeCount };

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool logarithmic;
    bool integer;
    bool boolean;
    bool output;
};

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"mode",       "Mode",          "",    0.0f,    1.0f,    0.0f, false, true,  false, false},
    {"balance",    "Balance",       "%",   0.0f,  100.0f,   50.0f, false, false, false, false},
    {"split",      "Stereo Split",  "%",   0.0f,  100.0f,    0.0f, false, false, false, false},
    {"slice",      "Slice",         "ms",  0.02f, 200.0f,    8.0f, true,  false, false, false},
    {"sync",       "Sync",          "",    0.0f,    2.0f,    0.0f, false, true,  false, false},
    {"periods",    "Periods",       "x",   0.25f,  16.0f,    1.0f, true,  false, false, false},
    {"crossfade",  "Crossfade",     "%",   0.0f,  100.0f,   25.0f, false, false, false, false},
    {"shape",      "Edge Shape",    "",    0.0f,    2.0f,    2.0f, false, true,  false, false},
    {"jitter",     "Jitter",        "%",   0.0f,  100.0f,    0.0f, false, false, false, false},
    {"scheme",     "Scheme",        "",    0.0f,    2.0f,    0.0f, false, true,  false, false},
    {"bin_width",  "Bin Width",     "bins",1.0f,   64.0f,    4.0f, true,  true,  false, false},
    {"drift",      "Drift",         "b/s", -200.0f, 200.0f,  0.0f, false, false, false, false},
    {"output",     "Output",        "dB", -24.0f,   12.0f,   0.0f, false, false, false, false},
    {"detected_hz","Detected Pitch","Hz",  0.0f, 2000.0f,    0.0f, false, false, false, true },
    {"cycle_ms",   "Cycle Length",  "ms",  0.0f,  800.0f,    0.0f, false, false, false, true },
}};

struct Parameters {
    float values[kParameterCount] {};
    Parameters()
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values[i] = kParameterSpecs[i].defaultValue;
    }
};

inline float& field(Parameters& p, std::uint32_t index) { return p.values[index]; }
inline const float& field(const Parameters& p, std::uint32_t index) { return p.values[index]; }

Parameters clampParameters(Parameters p);

}  // namespace downspout::interleaver
