#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::moka {

// ── Parameters ─────────────────────────────────────────────────────────────
// Eight controls. Voices selects the polyphony cap (1–12, default 4).

enum class ParamId : std::uint32_t {
    instrument = 0,
    decay,
    mallet,
    tone,
    spread,
    position,
    voices,
    level,
    release,
    width,
};

inline constexpr std::size_t kParameterCount = 10;
inline constexpr std::size_t kMaxVoices = 12;
inline constexpr std::size_t kModeCount = 8;

struct ParamSpec {
    const char* symbol;
    const char* name;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"instrument", "Instrument", 0.0f, 5.0f, 0.0f, true},
    {"decay", "Decay", 0.0f, 1.0f, 0.55f, false},
    {"mallet", "Mallet", 0.0f, 1.0f, 0.75f, false},
    {"tone", "Tone", 0.0f, 1.0f, 0.65f, false},
    {"spread", "Spread", 0.0f, 1.0f, 0.30f, false},
    {"position", "Position", 0.0f, 1.0f, 0.45f, false},
    {"voices", "Voices", 1.0f, 12.0f, 4.0f, true},
    {"level", "Level", 0.0f, 1.0f, 0.70f, false},
    {"release", "Release", 0.0f, 1.0f, 0.30f, false},
    {"width", "Width", 0.0f, 1.0f, 0.70f, false},
}};

inline constexpr std::array<const char*, 6> kInstrumentNames = {{
    "Xylophone",
    "Glockenspiel",
    "Woodblock",
    "Glass Bowl",
    "Metal Sheet",
    "Tube",
}};

// ── Modal tables ───────────────────────────────────────────────────────────
// Each instrument is a bank of damped sine modes: frequency ratio, relative
// level, and per-mode decay multiplier (upper partials die faster on bars,
// ring longer on glass/metal).

struct ModeEntry {
    float ratio = 0.0f;
    float level = 0.0f;
    float decayMul = 1.0f;
};

struct ModelSpec {
    const char* name;
    float baseT60; // seconds for the fundamental at Decay = 0.55-ish
    float transient; // transient click level 0–1
    std::array<ModeEntry, kModeCount> modes;
};

inline constexpr std::array<ModelSpec, 6> kModelSpecs = {{
    // Xylophone: rosewood bar, arched back — strong fundamental, fast upper
    // partials, ~1.4 s ring.
    {"Xylophone", 1.4f, 0.55f, {{
        {1.00f, 1.00f, 1.00f},
        {3.92f, 0.42f, 0.45f},
        {9.24f, 0.18f, 0.22f},
        {11.54f, 0.08f, 0.14f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
    }}},
    // Glockenspiel: steel bar — bright, long (~4.5 s), harmonic-ish upper modes.
    {"Glockenspiel", 4.5f, 0.70f, {{
        {1.00f, 1.00f, 1.00f},
        {2.76f, 0.55f, 0.70f},
        {5.40f, 0.32f, 0.50f},
        {8.93f, 0.16f, 0.32f},
        {13.20f, 0.07f, 0.20f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
    }}},
    // Woodblock: hollow slit drum — short (~0.28 s), woody knock, few modes.
    {"Woodblock", 0.28f, 0.90f, {{
        {1.00f, 1.00f, 1.00f},
        {2.32f, 0.50f, 0.60f},
        {3.85f, 0.22f, 0.35f},
        {5.60f, 0.08f, 0.20f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
    }}},
    // Glass Bowl: singing bowl — near-harmonic with a beating pair on the
    // fundamental, very long (~6 s) ring.
    {"Glass Bowl", 6.0f, 0.25f, {{
        {1.00f, 0.80f, 1.00f},
        {1.006f, 0.80f, 1.00f},
        {2.01f, 0.45f, 0.85f},
        {2.74f, 0.35f, 0.70f},
        {3.92f, 0.20f, 0.55f},
        {5.43f, 0.10f, 0.40f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
    }}},
    // Metal Sheet: struck plate — dense inharmonic cluster, ~3.2 s wash.
    {"Metal Sheet", 3.2f, 0.80f, {{
        {1.00f, 0.75f, 1.00f},
        {1.48f, 0.62f, 0.90f},
        {2.09f, 0.55f, 0.80f},
        {2.78f, 0.42f, 0.68f},
        {3.60f, 0.30f, 0.55f},
        {4.70f, 0.20f, 0.42f},
        {5.95f, 0.12f, 0.30f},
        {7.40f, 0.07f, 0.22f},
    }}},
    // Tube: soft flip-flop beater on a hollow tube — mellow hum + sparse
    // overtones, ~2.6 s, gentle attack.
    {"Tube", 2.6f, 0.35f, {{
        {1.00f, 1.00f, 1.00f},
        {1.50f, 0.35f, 0.70f},
        {2.00f, 0.50f, 0.75f},
        {2.76f, 0.28f, 0.55f},
        {4.20f, 0.12f, 0.35f},
        {6.80f, 0.05f, 0.22f},
        {0.00f, 0.00f, 1.00f},
        {0.00f, 0.00f, 1.00f},
    }}},
}};

// ── Mode weighting ──────────────────────────────────────────────────────────
// Spread, Mallet and Position do the same three things to every instrument
// table: they stretch the partial ratios away from their tabulated values,
// roll off upper partials according to beater hardness, and trade centre
// strike (round, fundamental-heavy) for edge strike (bright, biting). Both the
// engine and the panel's partial ladder read this, so the drawn spectrum
// cannot drift away from what is actually rendered.

struct ModeWeights {
    // Stretched frequency ratio, or 0 for an unused mode slot.
    std::array<float, kModeCount> ratio {};
    // Relative level before velocity scaling.
    std::array<float, kModeCount> weight {};
};

inline constexpr float clampUnit(const float value)
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

inline ModeWeights computeModeWeights(const int model, const float spread, const float mallet,
                                      const float position)
{
    const auto index = static_cast<std::size_t>(
        std::clamp(model, 0, static_cast<int>(kModelSpecs.size()) - 1));
    const ModelSpec& spec = kModelSpecs[index];

    // Spread stretches partials from near-harmonic (0.6x) to bell-like (1.5x).
    const float stretch = 0.60f + 0.90f * clampUnit(spread);
    // Hard beater excites upper partials; soft beater stays near the hum.
    const float spectralSlope = (1.80f - 1.74f * clampUnit(mallet)) * 0.50f;
    // Strike position: edge (0) is bright and clangorous, centre (1) is round.
    const float edgeMix = 1.0f - clampUnit(position);

    ModeWeights out {};
    for (std::size_t i = 0; i < kModeCount; ++i)
    {
        const ModeEntry& entry = spec.modes[i];
        if (entry.ratio <= 0.0f || entry.level <= 0.0f)
            continue;

        const float ratio = 1.0f + (entry.ratio - 1.0f) * stretch;
        float weight = entry.level * std::exp(-(ratio - 1.0f) * spectralSlope);
        // Centre strike damps partials proportionally to their distance
        // from the fundamental; edge strike lets them ring and adds bite.
        weight /= 1.0f + (1.0f - edgeMix) * (ratio - 1.0f) * 1.10f;
        weight *= 1.0f + edgeMix * std::min(ratio / 8.0f, 1.0f) * 0.90f;
        if (i == 0)
            weight *= 1.0f + (1.0f - edgeMix) * 0.25f;

        out.ratio[i] = ratio;
        out.weight[i] = weight;
    }
    return out;
}

// ── Ring-out ─────────────────────────────────────────────────────────────────
// Decay is a percentage of the instrument's own base ring, not an absolute
// time, so switching model changes how long the note lasts. The panel shows
// the resulting seconds rather than leaving the percentage to be guessed at.

inline constexpr float kMinRingT60 = 0.03f;

inline float decayScaleFor(const float decay)
{
    // 0.15x–3x around the instrument base.
    return 0.15f * std::pow(20.0f, clampUnit(decay));
}

inline float modelRingTimeSeconds(const int model, const float decay)
{
    const auto index = static_cast<std::size_t>(
        std::clamp(model, 0, static_cast<int>(kModelSpecs.size()) - 1));
    return std::max(kMinRingT60, kModelSpecs[index].baseT60 * decayScaleFor(decay));
}

// Resonator cutoff for one Tone setting, in hertz.
inline float toneCutoffHz(const float tone)
{
    const float t = clampUnit(tone);
    return 900.0f + t * t * 11000.0f;
}

// ── Factory presets ────────────────────────────────────────────────────────
// Named snapshots of the eight parameters. Selecting a preset in the UI
// writes all eight values back to the host.

struct Preset {
    const char* name;
    std::array<float, kParameterCount> values;
};

// instrument, decay, mallet, tone, spread, position, voices, level, release, width
inline constexpr std::array<Preset, 11> kPresets = {{
    {"Xylophone", {0.0f, 0.55f, 0.75f, 0.65f, 0.30f, 0.45f, 4.0f, 0.70f, 0.30f, 0.70f}},
    {"Glockenspiel", {1.0f, 0.70f, 0.85f, 0.80f, 0.30f, 0.40f, 4.0f, 0.62f, 0.55f, 0.80f}},
    {"Temple Blocks", {2.0f, 0.45f, 0.70f, 0.55f, 0.35f, 0.30f, 4.0f, 0.75f, 0.15f, 0.60f}},
    {"Glass Bowl", {3.0f, 0.85f, 0.30f, 0.70f, 0.15f, 0.60f, 4.0f, 0.66f, 0.70f, 0.85f}},
    {"Frost Glass", {3.0f, 0.95f, 0.55f, 0.90f, 0.25f, 0.35f, 6.0f, 0.60f, 0.80f, 0.90f}},
    {"Metal Sheet", {4.0f, 0.60f, 0.90f, 0.75f, 0.55f, 0.25f, 4.0f, 0.62f, 0.45f, 0.85f}},
    {"Thunder Plate", {4.0f, 0.90f, 0.65f, 0.45f, 0.70f, 0.55f, 6.0f, 0.66f, 0.65f, 0.90f}},
    {"Tube Flip-Flop", {5.0f, 0.55f, 0.25f, 0.50f, 0.30f, 0.55f, 4.0f, 0.74f, 0.35f, 0.60f}},
    {"Dark Tube", {5.0f, 0.70f, 0.20f, 0.30f, 0.20f, 0.70f, 4.0f, 0.72f, 0.45f, 0.55f}},
    {"Soft Xylo", {0.0f, 0.40f, 0.35f, 0.45f, 0.20f, 0.65f, 4.0f, 0.70f, 0.25f, 0.65f}},
    // Thumb piano: a short cantilevered steel tine plucked with the thumb
    // rather than hammered. Softer beater than the glockenspiel for a hollow
    // attack, near-harmonic stretch so the tine sits at about twice the
    // fundamental instead of ringing like a bell, and a short decay so a run
    // does not smear. Six voices suits a kalimba pattern.
    {"Kalimba", {1.0f, 0.31f, 0.58f, 0.80f, 0.05f, 0.22f, 6.0f, 0.68f, 0.34f, 0.58f}},
}};

} // namespace downspout::moka
