#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace downspout::drumkit {

struct ParameterSpec {
    const char* name;
    const char* symbol;
    float minimum;
    float maximum;
    float defaultValue;
    bool boolean;
    bool output = false;
};

enum ParameterId : std::uint32_t {
    kParamKickPitch = 0,
    kParamKickDecay,
    kParamKickDrive,
    kParamKickPunch,
    kParamKickLevel,
    kParamSnareTone,
    kParamSnareSnap,
    kParamSnareLevel,
    kParamClapDensity,
    kParamClapTone,
    kParamClapLevel,
    kParamTom1Pitch,
    kParamTom1Decay,
    kParamTom1Level,
    kParamTom2Pitch,
    kParamTom2Decay,
    kParamTom2Level,
    kParamClosedHHBrightness,
    kParamClosedHHDecay,
    kParamClosedHHLevel,
    kParamOpenHHBrightness,
    kParamOpenHHDecay,
    kParamOpenHHLevel,
    kParamCrashBrightness,
    kParamCrashDecay,
    kParamCrashLevel,
    kParamCowbellTone,
    kParamCowbellDecay,
    kParamCowbellLevel,
    kParamClaveTone,
    kParamClaveDecay,
    kParamClaveLevel,
    kParamBashSize,
    kParamBashSpread,
    kParamBashDecay,
    kParamBashDrive,
    kParamBashNoise,
    kParamBashEdge,
    kParamBashLevel,
    kParamBitCrush,
    kParamMasterDrive,
    kParamMasterReverb,
    kParamMasterGain,
    kParamKickMute,
    kParamClapMute,
    kParamSnareMute,
    kParamCrashMute,
    kParamClosedHHMute,
    kParamTom1Mute,
    kParamOpenHHMute,
    kParamTom2Mute,
    kParamBashMute,
    kParamCowbellMute,
    kParamClaveMute,
    kParamKickTransient,
    kParamClapMetal,
    kParamSnareMetal,
    kParamCrashMetal,
    kParamClosedHHMetal,
    kParamTom1Metal,
    kParamOpenHHMetal,
    kParamTom2Metal,
    kParamCowbellMetal,
    kParamClaveMetal,
    kParamKickPan,
    kParamClapPan,
    kParamSnarePan,
    kParamCrashPan,
    kParamClosedHHPan,
    kParamTom1Pan,
    kParamOpenHHPan,
    kParamTom2Pan,
    kParamBashPan,
    kParamCowbellPan,
    kParamClavePan,
    kParamKickTrig,
    kParamClapTrig,
    kParamSnareTrig,
    kParamCrashTrig,
    kParamClosedHHTrig,
    kParamTom1Trig,
    kParamOpenHHTrig,
    kParamTom2Trig,
    kParamBashTrig,
    kParamCowbellTrig,
    kParamClaveTrig,
    kParameterCount
};

enum class InstrumentId : std::uint8_t {
    Kick = 0,
    Clap,
    Snare,
    Crash,
    ClosedHH,
    Tom1,
    OpenHH,
    Tom2,
    Bash,
    Cowbell,
    Clave,
    Count
};

inline constexpr std::uint32_t kInstrumentCount = static_cast<std::uint32_t>(InstrumentId::Count);

struct InstrumentSpec {
    InstrumentId id;
    const char* name;
    const char* shortName;
    std::uint8_t midiNote;
    std::uint32_t levelParam;
    std::uint32_t muteParam;
    std::uint32_t panParam;
    std::uint32_t trigParam;
};

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"Kick Pitch", "kick_pitch", 0.0f, 1.0f, 0.35f, false},
    {"Kick Decay", "kick_decay", 0.0f, 1.0f, 0.4f, false},
    {"Kick Drive", "kick_drive", 0.0f, 1.0f, 0.3f, false},
    {"Kick Punch", "kick_punch", 0.0f, 1.0f, 0.15f, false},
    {"Kick Level", "kick_level", 0.0f, 1.5f, 1.0f, false},
    {"Snare Tone", "snare_tone", 0.0f, 1.0f, 0.5f, false},
    {"Snare Snap", "snare_snap", 0.0f, 1.0f, 0.6f, false},
    {"Snare Level", "snare_level", 0.0f, 1.5f, 1.05f, false},
    {"Clap Density", "clap_density", 0.0f, 1.0f, 0.55f, false},
    {"Clap Tone", "clap_tone", 0.0f, 1.0f, 0.45f, false},
    {"Clap Level", "clap_level", 0.0f, 1.5f, 1.0f, false},
    {"Tom 1 Pitch", "tom1_pitch", 0.0f, 1.0f, 0.4f, false},
    {"Tom 1 Decay", "tom1_decay", 0.0f, 1.0f, 0.45f, false},
    {"Tom 1 Level", "tom1_level", 0.0f, 1.5f, 0.74f, false},
    {"Tom 2 Pitch", "tom2_pitch", 0.0f, 1.0f, 0.55f, false},
    {"Tom 2 Decay", "tom2_decay", 0.0f, 1.0f, 0.45f, false},
    {"Tom 2 Level", "tom2_level", 0.0f, 1.5f, 0.72f, false},
    {"Closed HH Brightness", "hh_closed_brightness", 0.0f, 1.0f, 0.6f, false},
    {"Closed HH Decay", "hh_closed_decay", 0.0f, 1.0f, 0.3f, false},
    {"Closed HH Level", "hh_closed_level", 0.0f, 1.5f, 1.05f, false},
    {"Open HH Brightness", "hh_open_brightness", 0.0f, 1.0f, 0.7f, false},
    {"Open HH Decay", "hh_open_decay", 0.0f, 1.0f, 0.55f, false},
    {"Open HH Level", "hh_open_level", 0.0f, 1.5f, 1.02f, false},
    {"Crash Brightness", "crash_brightness", 0.0f, 1.0f, 0.65f, false},
    {"Crash Decay", "crash_decay", 0.0f, 1.0f, 0.5f, false},
    {"Crash Level", "crash_level", 0.0f, 1.5f, 1.0f, false},
    {"Cowbell Tone", "cowbell_tone", 0.0f, 1.0f, 0.45f, false},
    {"Cowbell Decay", "cowbell_decay", 0.0f, 1.0f, 0.35f, false},
    {"Cowbell Level", "cowbell_level", 0.0f, 1.5f, 1.0f, false},
    {"Clave Tone", "clave_tone", 0.0f, 1.0f, 0.5f, false},
    {"Clave Decay", "clave_decay", 0.0f, 1.0f, 0.25f, false},
    {"Clave Level", "clave_level", 0.0f, 1.5f, 1.0f, false},
    {"Bash Size", "bash_size", 0.0f, 1.0f, 0.45f, false},
    {"Bash Spread", "bash_spread", 0.0f, 1.0f, 0.55f, false},
    {"Bash Decay", "bash_decay", 0.0f, 1.0f, 0.7f, false},
    {"Bash Drive", "bash_drive", 0.0f, 1.0f, 0.65f, false},
    {"Bash Noise", "bash_noise", 0.0f, 1.0f, 0.6f, false},
    {"Bash Edge", "bash_edge", 0.0f, 1.0f, 0.7f, false},
    {"Bash Level", "bash_level", 0.0f, 1.5f, 1.0f, false},
    {"Bit Crush", "bit_crush", 0.0f, 1.0f, 0.0f, false},
    {"Master Drive", "master_drive", 0.0f, 1.0f, 0.25f, false},
    {"Master Reverb", "master_reverb", 0.0f, 1.0f, 0.2f, false},
    {"Master Gain", "master_gain", 0.0f, 1.0f, 0.7f, false},
    {"Kick Mute", "kick_mute", 0.0f, 1.0f, 0.0f, true},
    {"Clap Mute", "clap_mute", 0.0f, 1.0f, 0.0f, true},
    {"Snare Mute", "snare_mute", 0.0f, 1.0f, 0.0f, true},
    {"Crash Mute", "crash_mute", 0.0f, 1.0f, 0.0f, true},
    {"Closed HH Mute", "hh_closed_mute", 0.0f, 1.0f, 0.0f, true},
    {"Tom 1 Mute", "tom1_mute", 0.0f, 1.0f, 0.0f, true},
    {"Open HH Mute", "hh_open_mute", 0.0f, 1.0f, 0.0f, true},
    {"Tom 2 Mute", "tom2_mute", 0.0f, 1.0f, 0.0f, true},
    {"Bash Mute", "bash_mute", 0.0f, 1.0f, 0.0f, true},
    {"Cowbell Mute", "cowbell_mute", 0.0f, 1.0f, 0.0f, true},
    {"Clave Mute", "clave_mute", 0.0f, 1.0f, 0.0f, true},
    {"Kick Transient", "kick_transient", 0.0f, 1.0f, 0.0f, false},
    {"Clap Metal", "clap_metal", 0.0f, 1.0f, 0.0f, false},
    {"Snare Metal", "snare_metal", 0.0f, 1.0f, 0.0f, false},
    {"Crash Metal", "crash_metal", 0.0f, 1.0f, 0.0f, false},
    {"Closed HH Metal", "hh_closed_metal", 0.0f, 1.0f, 0.0f, false},
    {"Tom 1 Metal", "tom1_metal", 0.0f, 1.0f, 0.0f, false},
    {"Open HH Metal", "hh_open_metal", 0.0f, 1.0f, 0.0f, false},
    {"Tom 2 Metal", "tom2_metal", 0.0f, 1.0f, 0.0f, false},
    {"Cowbell Metal", "cowbell_metal", 0.0f, 1.0f, 0.0f, false},
    {"Clave Metal", "clave_metal", 0.0f, 1.0f, 0.0f, false},
    {"Kick Pan", "kick_pan", -1.0f, 1.0f, 0.00f, false},
    {"Clap Pan", "clap_pan", -1.0f, 1.0f, 0.16f, false},
    {"Snare Pan", "snare_pan", -1.0f, 1.0f, -0.08f, false},
    {"Crash Pan", "crash_pan", -1.0f, 1.0f, -0.44f, false},
    {"Closed HH Pan", "hh_closed_pan", -1.0f, 1.0f, -0.52f, false},
    {"Tom 1 Pan", "tom1_pan", -1.0f, 1.0f, 0.22f, false},
    {"Open HH Pan", "hh_open_pan", -1.0f, 1.0f, 0.32f, false},
    {"Tom 2 Pan", "tom2_pan", -1.0f, 1.0f, -0.16f, false},
    {"Bash Pan", "bash_pan", -1.0f, 1.0f, 0.42f, false},
    {"Cowbell Pan", "cowbell_pan", -1.0f, 1.0f, 0.14f, false},
    {"Clave Pan", "clave_pan", -1.0f, 1.0f, -0.24f, false},
    {"Kick Trig",      "kick_trig",      0.0f, 1.0f, 0.0f, false, true},
    {"Clap Trig",      "clap_trig",      0.0f, 1.0f, 0.0f, false, true},
    {"Snare Trig",     "snare_trig",     0.0f, 1.0f, 0.0f, false, true},
    {"Crash Trig",     "crash_trig",     0.0f, 1.0f, 0.0f, false, true},
    {"Closed HH Trig", "hh_closed_trig", 0.0f, 1.0f, 0.0f, false, true},
    {"Tom 1 Trig",     "tom1_trig",      0.0f, 1.0f, 0.0f, false, true},
    {"Open HH Trig",   "hh_open_trig",   0.0f, 1.0f, 0.0f, false, true},
    {"Tom 2 Trig",     "tom2_trig",      0.0f, 1.0f, 0.0f, false, true},
    {"Bash Trig",      "bash_trig",      0.0f, 1.0f, 0.0f, false, true},
    {"Cowbell Trig",   "cowbell_trig",   0.0f, 1.0f, 0.0f, false, true},
    {"Clave Trig",     "clave_trig",     0.0f, 1.0f, 0.0f, false, true},
}};

[[nodiscard]] inline float normalizedKickPitchToHz(const float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    constexpr float oldMinimumHz = 60.0f;
    constexpr float minimumHz = 30.0f;
    constexpr float maximumHz = 250.0f;
    constexpr float defaultNormalized = 0.35f;
    // std::pow is not constexpr on the libc++ shipped with the macOS runner.
    // This value is constant for the algorithm, but it need not be a
    // translation-time constant.
    const float defaultHz = oldMinimumHz * std::pow(maximumHz / oldMinimumHz, defaultNormalized);
    if (clamped <= defaultNormalized) {
        return minimumHz * std::pow(defaultHz / minimumHz, clamped / defaultNormalized);
    }
    return defaultHz * std::pow(maximumHz / defaultHz, (clamped - defaultNormalized) / (1.0f - defaultNormalized));
}

inline constexpr std::array<InstrumentSpec, kInstrumentCount> kInstrumentSpecs = {{
    {InstrumentId::Kick,     "Kick",       "KICK",  36, kParamKickLevel,     kParamKickMute,     kParamKickPan,     kParamKickTrig},
    {InstrumentId::Clap,     "Clap",       "CLAP",  39, kParamClapLevel,     kParamClapMute,     kParamClapPan,     kParamClapTrig},
    {InstrumentId::Snare,    "Snare",      "SNARE", 40, kParamSnareLevel,    kParamSnareMute,    kParamSnarePan,    kParamSnareTrig},
    {InstrumentId::Crash,    "Crash",      "CRASH", 41, kParamCrashLevel,    kParamCrashMute,    kParamCrashPan,    kParamCrashTrig},
    {InstrumentId::ClosedHH, "Closed Hat", "CHH",   42, kParamClosedHHLevel, kParamClosedHHMute, kParamClosedHHPan, kParamClosedHHTrig},
    {InstrumentId::Tom1,     "Low Tom",    "LTOM",  45, kParamTom1Level,     kParamTom1Mute,     kParamTom1Pan,     kParamTom1Trig},
    {InstrumentId::OpenHH,   "Open Hat",   "OHH",   46, kParamOpenHHLevel,   kParamOpenHHMute,   kParamOpenHHPan,   kParamOpenHHTrig},
    {InstrumentId::Tom2,     "High Tom",   "HTOM",  50, kParamTom2Level,     kParamTom2Mute,     kParamTom2Pan,     kParamTom2Trig},
    {InstrumentId::Bash,     "Bash",       "BASH",  51, kParamBashLevel,     kParamBashMute,     kParamBashPan,     kParamBashTrig},
    {InstrumentId::Cowbell,  "Cowbell",    "COW",   52, kParamCowbellLevel,  kParamCowbellMute,  kParamCowbellPan,  kParamCowbellTrig},
    {InstrumentId::Clave,    "Clave",      "CLAVE", 53, kParamClaveLevel,    kParamClaveMute,    kParamClavePan,    kParamClaveTrig},
}};

} // namespace downspout::drumkit
