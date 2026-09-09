#pragma once

#include <array>
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
};

inline constexpr std::size_t kParameterCount = 8;
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
    {"mallet", "Mallet", 0.0f, 1.0f, 0.65f, false},
    {"tone", "Tone", 0.0f, 1.0f, 0.60f, false},
    {"spread", "Spread", 0.0f, 1.0f, 0.30f, false},
    {"position", "Position", 0.0f, 1.0f, 0.45f, false},
    {"voices", "Voices", 1.0f, 12.0f, 4.0f, true},
    {"level", "Level", 0.0f, 1.0f, 0.70f, false},
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

// ── Factory presets ────────────────────────────────────────────────────────
// Named snapshots of the eight parameters. Selecting a preset in the UI
// writes all eight values back to the host.

struct Preset {
    const char* name;
    std::array<float, kParameterCount> values;
};

// instrument, decay, mallet, tone, spread, position, voices, level
inline constexpr std::array<Preset, 10> kPresets = {{
    {"Xylophone", {0.0f, 0.55f, 0.75f, 0.65f, 0.30f, 0.45f, 4.0f, 0.70f}},
    {"Glockenspiel", {1.0f, 0.70f, 0.85f, 0.80f, 0.30f, 0.40f, 4.0f, 0.62f}},
    {"Temple Blocks", {2.0f, 0.45f, 0.70f, 0.55f, 0.35f, 0.30f, 4.0f, 0.75f}},
    {"Glass Bowl", {3.0f, 0.85f, 0.30f, 0.70f, 0.15f, 0.60f, 4.0f, 0.66f}},
    {"Frost Glass", {3.0f, 0.95f, 0.55f, 0.90f, 0.25f, 0.35f, 6.0f, 0.60f}},
    {"Metal Sheet", {4.0f, 0.60f, 0.90f, 0.75f, 0.55f, 0.25f, 4.0f, 0.62f}},
    {"Thunder Plate", {4.0f, 0.90f, 0.65f, 0.45f, 0.70f, 0.55f, 6.0f, 0.66f}},
    {"Tube Flip-Flop", {5.0f, 0.55f, 0.25f, 0.50f, 0.30f, 0.55f, 4.0f, 0.74f}},
    {"Dark Tube", {5.0f, 0.70f, 0.20f, 0.30f, 0.20f, 0.70f, 4.0f, 0.72f}},
    {"Soft Xylo", {0.0f, 0.40f, 0.35f, 0.45f, 0.20f, 0.65f, 4.0f, 0.70f}},
}};

} // namespace downspout::moka
