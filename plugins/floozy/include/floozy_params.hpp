#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace downspout::floozy {

enum class ParamId : std::uint32_t {
    sourceAlgorithm = 0,
    sourceParam1,
    sourceParam2,
    sourceLevel,
    noiseLevel,
    dcLevel,
    attack,
    release,
    interfaceType,
    interfaceIntensity,
    tuning,
    ratio,
    delay1Feedback,
    delay2Feedback,
    filterFeedback,
    filterFrequency,
    filterQ,
    filterShape,
    lfoFrequency,
    modulationMix,
    reverbSize,
    reverbLevel,
    masterGain,
    numVoices,
    externalLevel,
    bodyType,
    conductorChannel,
};

inline constexpr std::size_t kParameterCount = 27;

struct ParamSpec {
    const char* symbol;
    const char* name;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"source_algorithm", "Source Algorithm", 0.0f, 7.0f, 3.0f, true},
    {"source_param_1", "Source Param 1", 0.0f, 1.0f, 0.55f, false},
    {"source_param_2", "Source Param 2", 0.0f, 1.0f, 0.50f, false},
    {"source_level", "Source Level", 0.0f, 1.0f, 0.50f, false},
    {"noise_level", "Noise Level", 0.0f, 1.0f, 0.20f, false},
    {"dc_level", "DC Level", 0.0f, 1.0f, 0.0f, false},
    {"attack", "Envelope Attack", 0.0f, 1.0f, 0.33f, false},
    {"release", "Envelope Release", 0.0f, 1.0f, 0.28f, false},
    {"interface_type", "Interface Type", 0.0f, 12.0f, 2.0f, true},
    {"interface_intensity", "Interface Intensity", 0.0f, 1.0f, 0.50f, false},
    {"tuning", "Delay Tuning", 0.0f, 1.0f, 0.50f, false},
    {"ratio", "Delay Ratio", 0.0f, 1.0f, 0.50f, false},
    {"delay_1_feedback", "Delay 1 Feedback", 0.0f, 1.0f, 0.50f, false},
    {"delay_2_feedback", "Delay 2 Feedback", 0.0f, 1.0f, 0.10f, false},
    {"filter_feedback", "Filter Feedback", 0.0f, 1.0f, 0.0f, false},
    {"filter_frequency", "Filter Frequency", 0.0f, 1.0f, 0.57f, false},
    {"filter_q", "Filter Q", 0.0f, 1.0f, 0.18f, false},
    {"filter_shape", "Filter Shape", 0.0f, 1.0f, 0.0f, false},
    {"lfo_frequency", "LFO Frequency", 0.0f, 1.0f, 0.74f, false},
    {"modulation_mix", "Modulation Mix", 0.0f, 1.0f, 0.50f, false},
    {"reverb_size", "Reverb Size", 0.0f, 1.0f, 0.50f, false},
    {"reverb_level", "Reverb Level", 0.0f, 1.0f, 0.30f, false},
    {"master_gain", "Master Gain", 0.0f, 1.0f, 0.80f, false},
    {"num_voices", "Num Voices", 1.0f, 8.0f, 4.0f, true},
    {"external_level", "External Level", 0.0f, 1.0f, 0.80f, false},
    {"body_type", "Body Type", 0.0f, 6.0f, 0.0f, true},
    {"conductor_ch", "Conductor Channel", 0.0f, 16.0f, 0.0f, true},
}};

inline constexpr std::array<const char*, 8> kSourceAlgorithmNames = {{
    "Dirichlet Pulse",
    "DSF Single",
    "DSF Double",
    "Tanh Square",
    "Tanh Saw",
    "PAF",
    "Modified FM",
    "None",
}};

inline constexpr std::array<std::array<const char*, 2>, 8> kSourceAlgorithmParamNames = {{
    {{"Harmonics", "Tilt"}},
    {{"Decay", "Ratio"}},
    {{"Decay", "Ratio"}},
    {{"Drive", "Trim"}},
    {{"Drive", "Blend"}},
    {{"Ratio", "Bandwidth"}},
    {{"Index", "Ratio"}},
    {{"—", "—"}},
}};

inline constexpr std::array<const char*, 13> kInterfaceTypeNames = {{
    "Pluck",
    "Hit",
    "Reed",
    "Flute",
    "Brass",
    "Bow",
    "Bell",
    "Drum",
    "Crystal",
    "Vapor",
    "Quantum",
    "Plasma",
    "Tube",
}};

inline constexpr std::array<const char*, 7> kBodyTypeNames = {{
    "Delays",
    "Beam",
    "Marimba",
    "Drumhead",
    "Membrane",
    "Plate",
    "String",
}};

} // namespace downspout::floozy
