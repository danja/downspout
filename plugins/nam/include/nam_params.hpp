#pragma once

#include <array>
#include <cstdint>

namespace downspout::namfx {

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
    bool logarithmic = false;
};

// Append-only: host sessions store parameters by symbol, but automation lanes use the index.
enum Param : std::uint32_t {
    kInputGain,
    kGate,
    kBass,
    kMid,
    kTreble,
    kIrLowCut,
    kIrHighCut,
    kIrGain,
    kOutputGain,
    kMix,
    kChannels,
    kModelEnable,
    kIrEnable,
    kNormalize,
    // Outputs (read-only status for the panel)
    kInLevel,
    kOutLevel,
    kStatusModel,
    kStatusIr,
    kModelRate,
    kIrMs,
    kParameterCount
};

enum ChannelMode : int { kMonoSum = 0, kStereo = 1 };
enum SlotStatus : int { kSlotEmpty = 0, kSlotLoaded = 1, kSlotError = 2 };

inline constexpr float kGateOff = -90.0f;     // gate threshold at the minimum means "off"
inline constexpr float kLowCutOff = 20.0f;    // IR low cut at the minimum means "off"
inline constexpr float kHighCutOff = 20000.0f;
inline constexpr float kNormalizeTargetDb = -18.0f;  // loudness every normalised model is brought to

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"input_gain", "Input gain", "dB", -24.0f, 24.0f, 0.0f},
    {"gate", "Noise gate", "dB", kGateOff, -20.0f, kGateOff},
    {"bass", "Bass", "dB", -12.0f, 12.0f, 0.0f},
    {"mid", "Mid", "dB", -12.0f, 12.0f, 0.0f},
    {"treble", "Treble", "dB", -12.0f, 12.0f, 0.0f},
    {"ir_low_cut", "IR low cut", "Hz", kLowCutOff, 500.0f, kLowCutOff, false, false, true},
    {"ir_high_cut", "IR high cut", "Hz", 2000.0f, kHighCutOff, kHighCutOff, false, false, true},
    {"ir_gain", "IR gain", "dB", -24.0f, 12.0f, 0.0f},
    {"output_gain", "Output gain", "dB", -24.0f, 24.0f, 0.0f},
    {"mix", "Mix", "", 0.0f, 1.0f, 1.0f},
    {"channels", "Channels", "", 0.0f, 1.0f, 0.0f, true},
    {"model_enable", "Model on", "", 0.0f, 1.0f, 1.0f, true},
    {"ir_enable", "IR on", "", 0.0f, 1.0f, 1.0f, true},
    {"normalize", "Normalise model", "", 0.0f, 1.0f, 1.0f, true},
    {"in_level", "Input level", "dB", -90.0f, 12.0f, -90.0f, false, true},
    {"out_level", "Output level", "dB", -90.0f, 12.0f, -90.0f, false, true},
    {"status_model", "Model status", "", 0.0f, 2.0f, 0.0f, true, true},
    {"status_ir", "IR status", "", 0.0f, 2.0f, 0.0f, true, true},
    {"model_rate", "Model rate", "Hz", 0.0f, 192000.0f, 0.0f, true, true},
    {"ir_ms", "IR length", "ms", 0.0f, 4000.0f, 0.0f, true, true},
}};

}  // namespace downspout::namfx
