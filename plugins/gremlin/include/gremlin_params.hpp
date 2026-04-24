#pragma once

#include <array>
#include <cstdint>

namespace downspout::gremlin {

inline constexpr std::size_t kLiveParamCount = 17;
inline constexpr std::size_t kHiddenParamCount = 8;
inline constexpr std::size_t kMacroCount = 8;
inline constexpr std::size_t kMomentaryCount = 8;
inline constexpr std::size_t kEffectiveParamCount = kLiveParamCount + kHiddenParamCount;
inline constexpr std::size_t kSceneCount = 4;

enum class LiveParamId : std::uint32_t {
    mode = 0,
    damage,
    chaos,
    noise,
    drift,
    crunch,
    fold,
    delayTime,
    feedback,
    warp,
    stutter,
    tone,
    damping,
    space,
    attack,
    release,
    output
};

enum class HiddenParamId : std::uint32_t {
    sourceGain = 0,
    burst,
    pitchSpread,
    delayMix,
    crossFeedback,
    glitchLength,
    chaosRate,
    duck
};

enum class MacroId : std::uint32_t {
    source = 0,
    pitch,
    breakage,
    delay,
    space,
    stutter,
    tone,
    output
};

enum class MomentaryId : std::uint32_t {
    freeze = 0,
    stutterMax,
    chaosBoost,
    crunchBlast,
    feedbackPush,
    warpPush,
    noiseBurst,
    duckKill
};

enum class ActionId : std::uint32_t {
    reseed = 0,
    burst,
    randomSource,
    randomDelay,
    randomAll,
    panic
};

enum class SceneId : std::uint32_t {
    manual = 0,
    splinter = 1,
    melt = 2,
    rust = 3,
    tunnel = 4
};

struct ParamSpec {
    const char* symbol;
    const char* name;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool boolean = false;
};

inline constexpr std::array<ParamSpec, kLiveParamCount> kLiveParamSpecs = {{
    {"mode", "Mode", 0.0f, 3.0f, 0.0f, true, false},
    {"damage", "Damage", 0.0f, 1.0f, 0.34f, false, false},
    {"chaos", "Chaos", 0.0f, 1.0f, 0.38f, false, false},
    {"noise", "Noise", 0.0f, 1.0f, 0.10f, false, false},
    {"drift", "Drift", 0.0f, 1.0f, 0.18f, false, false},
    {"crunch", "Crunch", 0.0f, 1.0f, 0.18f, false, false},
    {"fold", "Fold", 0.0f, 1.0f, 0.18f, false, false},
    {"delay_time", "Delay Time", 0.0f, 1.0f, 0.24f, false, false},
    {"feedback", "Feedback", 0.0f, 1.0f, 0.34f, false, false},
    {"warp", "Warp", 0.0f, 1.0f, 0.24f, false, false},
    {"stutter", "Stutter", 0.0f, 1.0f, 0.20f, false, false},
    {"tone", "Tone", 0.0f, 1.0f, 0.76f, false, false},
    {"damping", "Damping", 0.0f, 1.0f, 0.60f, false, false},
    {"space", "Space", 0.0f, 1.0f, 0.32f, false, false},
    {"attack", "Attack", 0.0f, 1.0f, 0.02f, false, false},
    {"release", "Release", 0.0f, 1.0f, 0.12f, false, false},
    {"output", "Output", 0.0f, 1.0f, 0.40f, false, false},
}};

inline constexpr std::array<ParamSpec, kHiddenParamCount> kHiddenParamSpecs = {{
    {"source_gain", "Source Gain", 0.0f, 1.0f, 0.62f, false, false},
    {"burst", "Burst", 0.0f, 1.0f, 0.72f, false, false},
    {"pitch_spread", "Pitch Spread", 0.0f, 1.0f, 0.24f, false, false},
    {"delay_mix", "Delay Mix", 0.0f, 1.0f, 0.30f, false, false},
    {"cross_feedback", "Cross Feedback", 0.0f, 1.0f, 0.28f, false, false},
    {"glitch_length", "Glitch Length", 0.0f, 1.0f, 0.22f, false, false},
    {"chaos_rate", "Chaos Rate", 0.0f, 1.0f, 0.32f, false, false},
    {"duck", "Duck", 0.0f, 1.0f, 0.32f, false, false},
}};

inline constexpr std::array<ParamSpec, kMacroCount> kMacroParamSpecs = {{
    {"macro_source", "Macro Source", 0.0f, 1.0f, 0.50f, false, false},
    {"macro_pitch", "Macro Pitch", 0.0f, 1.0f, 0.50f, false, false},
    {"macro_breakage", "Macro Breakage", 0.0f, 1.0f, 0.50f, false, false},
    {"macro_delay", "Macro Delay", 0.0f, 1.0f, 0.50f, false, false},
    {"macro_space", "Macro Space", 0.0f, 1.0f, 0.50f, false, false},
    {"macro_stutter", "Macro Stutter", 0.0f, 1.0f, 0.50f, false, false},
    {"macro_tone", "Macro Tone", 0.0f, 1.0f, 0.50f, false, false},
    {"macro_output", "Macro Output", 0.0f, 1.0f, 0.50f, false, false},
}};

inline constexpr std::array<ParamSpec, kMomentaryCount> kMomentaryParamSpecs = {{
    {"momentary_freeze", "Freeze", 0.0f, 1.0f, 0.0f, true, true},
    {"momentary_stutter_max", "Stutter Max", 0.0f, 1.0f, 0.0f, true, true},
    {"momentary_chaos_boost", "Chaos Boost", 0.0f, 1.0f, 0.0f, true, true},
    {"momentary_crunch_blast", "Crunch Blast", 0.0f, 1.0f, 0.0f, true, true},
    {"momentary_feedback_push", "Feedback Push", 0.0f, 1.0f, 0.0f, true, true},
    {"momentary_warp_push", "Warp Push", 0.0f, 1.0f, 0.0f, true, true},
    {"momentary_noise_burst", "Noise Burst", 0.0f, 1.0f, 0.0f, true, true},
    {"momentary_duck_kill", "Duck Kill", 0.0f, 1.0f, 0.0f, true, true},
}};

inline constexpr std::array<const char*, 4> kModeNames = {{
    "Shard",
    "Servo",
    "Spray",
    "Collapse",
}};

inline constexpr std::array<const char*, 5> kSceneNames = {{
    "Manual",
    "Splinter",
    "Melt",
    "Rust",
    "Tunnel",
}};

inline constexpr std::array<const char*, 8> kMacroNames = {{
    "Source",
    "Pitch",
    "Breakage",
    "Delay",
    "Space",
    "Stutter",
    "Tone",
    "Output",
}};

inline constexpr std::array<const char*, 8> kMomentaryNames = {{
    "Freeze",
    "Stutter Max",
    "Chaos Boost",
    "Crunch Blast",
    "Feedback Push",
    "Warp Push",
    "Noise Burst",
    "Duck Kill",
}};

inline constexpr std::array<const char*, 6> kActionNames = {{
    "Reseed",
    "Burst",
    "Rand Source",
    "Rand Delay",
    "Rand All",
    "Panic",
}};

inline constexpr std::array<const char*, kEffectiveParamCount> kStatusNames = {{
    "Live Mode",
    "Live Damage",
    "Live Chaos",
    "Live Noise",
    "Live Drift",
    "Live Crunch",
    "Live Fold",
    "Live Delay Time",
    "Live Feedback",
    "Live Warp",
    "Live Stutter",
    "Live Tone",
    "Live Damping",
    "Live Space",
    "Live Attack",
    "Live Release",
    "Live Output",
    "Live Source Gain",
    "Live Burst",
    "Live Pitch Spread",
    "Live Delay Mix",
    "Live Cross Feedback",
    "Live Glitch Length",
    "Live Chaos Rate",
    "Live Duck",
}};

inline constexpr std::array<std::uint8_t, 16> kPrimaryKnobCCs = {{
    16, 20, 24, 28, 46, 50, 54, 58,
    17, 21, 25, 29, 47, 51, 55, 59,
}};

inline constexpr std::array<std::uint8_t, 8> kHiddenKnobCCs = {{
    18, 22, 26, 30, 48, 52, 56, 60,
}};

inline constexpr std::array<std::uint8_t, 8> kMacroFaderCCs = {{
    19, 23, 27, 31, 49, 53, 57, 61,
}};

inline constexpr std::uint8_t kMasterFaderCC = 62;

inline constexpr std::array<std::uint8_t, 8> kMuteNotes = {{
    1, 4, 7, 10, 13, 16, 19, 22,
}};

inline constexpr std::array<std::uint8_t, 8> kSoloMuteNotes = {{
    2, 5, 8, 11, 14, 17, 20, 23,
}};

inline constexpr std::array<std::uint8_t, 8> kRecArmNotes = {{
    3, 6, 9, 12, 15, 18, 21, 24,
}};

inline constexpr std::uint8_t kBankLeftNote = 25;
inline constexpr std::uint8_t kBankRightNote = 26;
inline constexpr std::uint8_t kSoloNote = 27;

inline constexpr std::array<LiveParamId, 16> kPrimaryKnobTargets = {{
    LiveParamId::damage,
    LiveParamId::chaos,
    LiveParamId::noise,
    LiveParamId::drift,
    LiveParamId::crunch,
    LiveParamId::fold,
    LiveParamId::attack,
    LiveParamId::release,
    LiveParamId::delayTime,
    LiveParamId::feedback,
    LiveParamId::warp,
    LiveParamId::stutter,
    LiveParamId::tone,
    LiveParamId::damping,
    LiveParamId::space,
    LiveParamId::output,
}};

inline constexpr std::array<HiddenParamId, 8> kHiddenKnobTargets = {{
    HiddenParamId::sourceGain,
    HiddenParamId::burst,
    HiddenParamId::pitchSpread,
    HiddenParamId::delayMix,
    HiddenParamId::crossFeedback,
    HiddenParamId::glitchLength,
    HiddenParamId::chaosRate,
    HiddenParamId::duck,
}};

}  // namespace downspout::gremlin
