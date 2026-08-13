#pragma once

#include <cstdint>

namespace downspout::midiscribe {

// Parameter IDs — keep in sync with MidiscribePlugin::initParameter
enum ParamId : uint32_t {
    kParamArmed        = 0,   // bool: 0=off, 1=on
    kParamWrite        = 1,   // trigger: momentary float 0→1
    kParamCaptureBeats = 2,   // enum: 8, 16, 32, 64 (default 16)
    kParamCount
};

// State key for export path
constexpr const char* kStateKeyExportPath = "exportPath";
constexpr const char* kDefaultExportPath  = "/tmp/midiscribe.mid";

// Capture beat options
constexpr int kCaptureBeatValues[] = {8, 16, 32, 64};
constexpr int kCaptureBeatCount    = 4;
constexpr int kDefaultCaptureBeatIndex = 1;  // index into kCaptureBeatValues → 16

// Internal ring buffer capacity (events, not beats)
constexpr int kMaxCapturedEvents = 16384;

// SMF resolution
constexpr int kSmfPpq = 480;

// Default BPM if transport position is unavailable
constexpr double kDefaultBpm = 120.0;

}  // namespace downspout::midiscribe
