#pragma once

#include "modules/CycleFunctions.hpp"
#include "modules/Cylinder.hpp"
#include "modules/ExhaustSystem.hpp"
#include "modules/Noise.hpp"
#include "modules/Waveguide.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace downspout::magneto {

inline constexpr int kControlUpdatePeriod = 64;
inline constexpr float kMaxSupportedSampleRate = 192000.0f;

// Target round-trip gains per element. These, the reflection clamp, and the
// 1/sqrt(M) junctions are what bound the network; see docs/design.md.
inline constexpr float kChamberRoundTrip = 0.88f;
inline constexpr float kRunnerRoundTrip = 0.93f;
inline constexpr float kPipeRoundTrip = 0.97f;
inline constexpr float kMufflerRoundTrip = 0.95f;
inline constexpr float kOutletRoundTrip = 0.97f;

inline constexpr float kGeometrySmoothingMs = 30.0f;
inline constexpr float kIgnitionBaseAmp = 0.5f;
inline constexpr float kIgnitionIdleFloor = 0.12f;
inline constexpr float kBackfireGain = 0.6f;

// Per-tap makeup. The three model outputs leave the network at very different
// levels: the intake collectors radiate directly, the block tap is a filtered
// sum of mechanical motion, and the exhaust reaches the tailpipe only after the
// manifold junction, the pipe, the muffler and the outlet. These constants put
// them on comparable footing and bring a default patch to a usable level; they
// are measured, not derived. See docs/design.md.
inline constexpr float kIntakeMakeup = 25.0f;
inline constexpr float kBlockMakeup = 0.5f;
inline constexpr float kExhaustMakeup = 400.0f;
inline constexpr std::uint32_t kNonFinitePanicCount = 16;

// ── Listening position ─────────────────────────────────────────────────────
//
// Per-source weight and stereo placement for each vantage point. The three
// paper gains (intake/block/outlet) are applied before this table, so the
// position control is purely a change of perspective.

struct ListenMix {
    float wIntake;
    float wBlock;
    float wExhaust;
    float pIntake;  // -1 left .. +1 right
    float pBlock;
    float pExhaust;
    float width;
};

inline constexpr std::array<ListenMix, 4> kListenTable = {{
    /* Cabin    */ {0.35f, 1.00f, 0.45f, -0.15f, 0.00f, 0.25f, 0.35f},
    /* Front    */ {1.00f, 0.55f, 0.30f, 0.00f, -0.10f, 0.40f, 0.80f},
    /* Rear     */ {0.15f, 0.35f, 1.00f, -0.40f, 0.00f, 0.00f, 0.70f},
    /* Exterior */ {0.60f, 0.45f, 0.85f, -0.55f, 0.00f, 0.55f, 1.00f},
}};

// ── Host-neutral inputs ────────────────────────────────────────────────────

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double bpm = 120.0;
};

struct InputMidiEvent {
    std::uint32_t frame = 0;
    std::uint8_t data[3] = {};
};

// ── Parameters ─────────────────────────────────────────────────────────────
//
// Field order matches ParamId in magneto_params.hpp. Defaults here and in
// kParameterSpecs must agree; magneto_core_tests.cpp asserts that.

struct Parameters {
    // Engine
    float cylinders = 4.0f;
    float displacement = 500.0f;  // cc per cylinder
    float compression = 10.0f;    // ratio :1
    float ignition = 0.15f;       // explosion width, fraction of the power half
    float asymmetry = 0.12f;      // "Growl"
    float blockGain = 0.35f;
    // Intake
    float intakeLen = 0.35f;  // metres
    float intakeGain = 0.50f;
    float turbulence = 0.60f;
    // Exhaust
    float extractorLen = 0.60f;  // metres
    float pipeLen = 1.80f;       // metres
    float mufflerLen = 0.50f;    // metres
    float mufflerAction = 0.60f;
    float outletLen = 0.25f;  // metres
    float outletGain = 0.80f;
    float backfire = 0.20f;
    // Drive
    float rpm = 850.0f;
    float throttle = 0.0f;
    float rpmSource = 0.0f;  // 0 = Manual, 1 = Host Sync
    float syncRatio = 3.0f;  // index into kSyncRatioValues
    float idleRpm = 800.0f;
    float inertia = 400.0f;  // milliseconds
    float seed = 1.0f;
    float midiCh = 17.0f;  // 0 = off, 1-16 = channel, 17 = all
    // Output
    float listen = 0.0f;  // index into kListenTable
    float width = 0.60f;
    float level = 0.75f;
};

// ── Engine state ───────────────────────────────────────────────────────────
//
// Around 315 KB of delay memory. This must never be a stack local: the DPF
// wrapper holds it as a plugin member and the tests allocate it with
// std::make_unique.

struct EngineState {
    double sampleRate = 48000.0;

    std::array<CylinderState, kMaxCylinders> cylinders {};
    ExhaustState exhaust {};

    Phasor phasor {};
    std::array<float, kMaxCylinders> phaseOffset {};

    Lcg rngNoise {};
    Lcg rngBackfire {};
    OnePoleLowpass noiseLp {};

    OnePoleLowpass blockLp1 {};
    OnePoleLowpass blockLp2 {};
    DCBlocker dcIntake {};
    DCBlocker dcBlock {};
    DCBlocker dcExhaust {};

    // Speed and load, resolved at control rate
    float rpm = 800.0f;
    float rpmPrevious = 800.0f;
    float phaseIncrement = 0.0f;
    bool decelerating = false;
    float throttle = 0.0f;
    float ignitionAmp = 0.0f;
    float blockDrive = 0.35f;

    // Smoothed geometry, in one-way delay samples
    Smoother chamberDelay {};
    Smoother intakeDelay {};
    Smoother extractorDelay {};
    Smoother pipeDelay {};
    Smoother mufflerDelay {};
    Smoother outletDelay {};
    float geometryCoefficient = 0.01f;
    float invCompression = 0.1f;

    // Cached control-rate values
    int activeCylinders = 4;
    float invActive = 0.25f;
    float invSqrtActive = 0.5f;
    float mufflerBeta = -0.6f;
    float damping = 0.5f;
    float noiseCoefficient = 0.3f;
    float blockCoefficient = 0.03f;
    float lampDecay = 0.9999f;
    std::array<float, kMufflerElements> mufflerTargets {};
    ListenMix mix = kListenTable[0];
    float levelGain = 0.75f;
    float widthAmount = 0.6f;
    float intakeGain = 0.5f;
    float blockGain = 0.35f;
    float exhaustGain = 0.8f;
    // Equal-power pan gains, recomputed with the listening position
    float panIntakeL = 0.707f;
    float panIntakeR = 0.707f;
    float panBlockL = 0.707f;
    float panBlockR = 0.707f;
    float panExhaustL = 0.707f;
    float panExhaustR = 0.707f;
    float appliedSeed = 0.0f;

    // Backfire
    float backfireProbability = 0.0f;
    bool backfireActive = false;
    float backfirePhase = 0.0f;
    float backfireIncrement = 0.0f;
    float backfireAmp = 0.0f;
    float backfireLamp = 0.0f;

    // Status and diagnostics
    std::uint32_t cycleCount = 0;
    std::uint32_t backfireCount = 0;
    std::uint32_t backfireFirstFrame = 0;
    std::uint32_t nonFinite = 0;
    int controlCounter = 0;
};

static_assert(std::is_trivially_copyable_v<Parameters>, "Parameters must stay a POD");
static_assert(std::is_trivially_copyable_v<EngineState>, "EngineState must stay allocation-free");
static_assert(sizeof(EngineState) < 512u * 1024u, "EngineState grew unexpectedly large");

}  // namespace downspout::magneto
