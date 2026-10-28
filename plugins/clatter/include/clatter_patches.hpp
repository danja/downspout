#pragma once

#include "clatter_params.hpp"

namespace clatter {

// One General MIDI percussion sound. The three p* values mean different things per family:
//   Membrane  p1 pitch-drop ratio  p2 upper-mode level   p3 metallic ring level (timbales)
//   Metal     p1 partial set       p2 strikes            p3 strike spacing, ms
//   Wood      p1 click level       p2 strikes            p3 strike spacing, ms
//   Shaker    p1 attack, ms        p2 rattle rate, Hz    p3 band Q
//   Clap      p1 strikes (0 = scrape)  p2 spacing ms (clap) / scrape rate Hz (guiro)  p3 band Q
// `decay` is the time to fall 60 dB (a scrape's length, for the guiros); `group` > 0 means a new hit in the
// group chokes the others.
struct Patch {
    int note;
    const char* name;
    Family family;
    float freq;
    float decay;
    float pan;
    int group;
    float p1, p2, p3;
    float gain;
};

// Table order is the Sound parameter order: families in turn, then by GM note within a family.
inline constexpr Patch kPatches[kSoundCount] = {
    {41, "Low Floor Tom",  Family::Membrane,  82.0f, 0.50f,  0.55f, 0, 0.55f, 0.35f, 0.00f, 0.83f},
    {43, "High Floor Tom", Family::Membrane,  98.0f, 0.46f,  0.40f, 0, 0.55f, 0.35f, 0.00f, 0.87f},
    {45, "Low Tom",        Family::Membrane, 118.0f, 0.42f,  0.15f, 0, 0.55f, 0.35f, 0.00f, 0.91f},
    {47, "Low-Mid Tom",    Family::Membrane, 140.0f, 0.38f, -0.10f, 0, 0.55f, 0.35f, 0.00f, 0.96f},
    {48, "Hi-Mid Tom",     Family::Membrane, 165.0f, 0.35f, -0.30f, 0, 0.55f, 0.35f, 0.00f, 1.00f},
    {50, "High Tom",       Family::Membrane, 196.0f, 0.32f, -0.50f, 0, 0.55f, 0.35f, 0.00f, 1.04f},
    {60, "Hi Bongo",       Family::Membrane, 520.0f, 0.20f, -0.45f, 0, 0.25f, 0.50f, 0.00f, 1.01f},
    {61, "Low Bongo",      Family::Membrane, 390.0f, 0.26f, -0.25f, 0, 0.25f, 0.50f, 0.00f, 0.99f},
    {62, "Mute Hi Conga",  Family::Membrane, 340.0f, 0.10f,  0.30f, 1, 0.18f, 0.25f, 0.00f, 1.24f},
    {63, "Open Hi Conga",  Family::Membrane, 340.0f, 0.40f,  0.30f, 1, 0.18f, 0.45f, 0.00f, 0.92f},
    {64, "Low Conga",      Family::Membrane, 250.0f, 0.50f,  0.50f, 0, 0.18f, 0.45f, 0.00f, 0.82f},
    {65, "Hi Timbale",     Family::Membrane, 560.0f, 0.30f, -0.55f, 0, 0.12f, 0.30f, 0.25f, 1.02f},
    {66, "Low Timbale",    Family::Membrane, 420.0f, 0.36f, -0.35f, 0, 0.12f, 0.30f, 0.25f, 0.98f},
    {86, "Mute Surdo",     Family::Membrane,  72.0f, 0.22f,  0.00f, 3, 0.35f, 0.15f, 0.00f, 1.28f},
    {87, "Open Surdo",     Family::Membrane,  72.0f, 0.90f,  0.00f, 3, 0.35f, 0.20f, 0.00f, 0.64f},

    {67, "Hi Agogo",       Family::Metal,    920.0f, 0.45f,  0.45f, 0, 0.0f, 1.0f,  0.0f, 1.46f},
    {68, "Low Agogo",      Family::Metal,    690.0f, 0.50f,  0.55f, 0, 0.0f, 1.0f,  0.0f, 1.42f},
    {80, "Mute Triangle",  Family::Metal,   3200.0f, 0.07f,  0.25f, 0, 1.0f, 1.0f,  0.0f, 1.68f},
    {83, "Jingle Bell",    Family::Metal,   4300.0f, 0.50f,  0.35f, 0, 2.0f, 4.0f, 15.0f, 0.93f},
    {84, "Bell Tree",      Family::Metal,   2600.0f, 1.00f, -0.20f, 0, 3.0f, 9.0f, 17.0f, 0.75f},

    {75, "Claves",         Family::Wood,    2450.0f, 0.09f, -0.20f, 0, 0.25f, 1.0f,  0.0f, 1.01f},
    {76, "Hi Wood Block",  Family::Wood,    1750.0f, 0.11f, -0.35f, 0, 0.35f, 1.0f,  0.0f, 0.98f},
    {77, "Low Wood Block", Family::Wood,    1150.0f, 0.14f, -0.15f, 0, 0.35f, 1.0f,  0.0f, 0.98f},
    {85, "Castanets",      Family::Wood,    2900.0f, 0.06f,  0.50f, 0, 0.90f, 2.0f, 13.0f, 0.80f},

    {69, "Cabasa",         Family::Shaker,  6500.0f, 0.10f, -0.50f, 0, 12.0f,  0.0f, 1.2f, 0.46f},
    {70, "Maracas",        Family::Shaker,  7000.0f, 0.07f,  0.45f, 0,  4.0f,  0.0f, 1.4f, 0.63f},
    {82, "Shaker",         Family::Shaker,  5600.0f, 0.12f, -0.30f, 0, 20.0f,  0.0f, 1.0f, 0.51f},
    {58, "Vibraslap",      Family::Shaker,  1500.0f, 0.95f,  0.10f, 0,  2.0f, 52.0f, 1.8f, 1.26f},

    {39, "Hand Clap",      Family::Clap,    1100.0f, 0.22f,  0.00f, 0, 4.0f,  9.0f, 1.4f, 1.33f},
    {73, "Short Guiro",    Family::Clap,    2700.0f, 0.13f,  0.40f, 2, 0.0f, 58.0f, 1.8f, 1.45f},
    {74, "Long Guiro",     Family::Clap,    2500.0f, 0.55f,  0.40f, 2, 0.0f, 46.0f, 1.8f, 1.27f},
};

}  // namespace clatter
