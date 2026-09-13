// These are behaviour assertions, not debug aids: keep them in Release builds,
// which is what install.sh and package-release.sh use.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "magneto_engine.hpp"
#include "magneto_params.hpp"
#include "magneto_serialization.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <numeric>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace downspout::magneto;

namespace {

constexpr double kSampleRate = 48000.0;

std::atomic<long> gAllocations {0};
std::atomic<bool> gCountAllocations {false};

// ── Helpers ────────────────────────────────────────────────────────────────

std::unique_ptr<EngineState> makeEngine(const double sampleRate = kSampleRate)
{
    auto state = std::make_unique<EngineState>();
    activate(*state, sampleRate);
    return state;
}

// Render `frames` samples in blocks, returning the left channel.
std::vector<float> render(EngineState& state,
                          const Parameters& params,
                          const TransportSnapshot& transport,
                          const std::size_t frames,
                          const std::size_t blockSize = 256,
                          const double sampleRate = kSampleRate)
{
    std::vector<float> left(frames, 0.0f);
    std::vector<float> right(frames, 0.0f);
    std::size_t done = 0;
    while (done < frames)
    {
        const std::size_t n = std::min(blockSize, frames - done);
        processBlock(state, params, transport, static_cast<std::uint32_t>(n), sampleRate,
                     left.data() + done, right.data() + done);
        done += n;
    }
    return left;
}

double rms(const std::vector<float>& x)
{
    double sum = 0.0;
    for (const float v : x)
        sum += static_cast<double>(v) * static_cast<double>(v);
    return x.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(x.size()));
}

// Single-bin magnitude, so spectral assertions need no FFT dependency.
double goertzel(const std::vector<float>& x, const double frequency, const double sampleRate)
{
    const double omega = 2.0 * static_cast<double>(kPi) * frequency / sampleRate;
    const double coefficient = 2.0 * std::cos(omega);
    double s1 = 0.0;
    double s2 = 0.0;
    for (const float v : x)
    {
        const double s0 = static_cast<double>(v) + coefficient * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    return std::sqrt(std::max(power, 0.0)) / static_cast<double>(x.size());
}

// Energy above roughly 1 kHz, via a one-pole high-pass. Enough to tell a
// bright vantage from a dark one without an FFT.
double highFrequencyRms(const std::vector<float>& x, const double sampleRate)
{
    const double a = std::exp(-2.0 * static_cast<double>(kPi) * 1000.0 / sampleRate);
    double previousIn = 0.0;
    double previousOut = 0.0;
    double sum = 0.0;
    for (const float v : x)
    {
        previousOut = a * (previousOut + static_cast<double>(v) - previousIn);
        previousIn = static_cast<double>(v);
        sum += previousOut * previousOut;
    }
    return x.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(x.size()));
}

bool allFinite(const std::vector<float>& x, const float bound)
{
    for (const float v : x)
    {
        if (!std::isfinite(v) || std::fabs(v) > bound)
            return false;
    }
    return true;
}

// ── Tests ──────────────────────────────────────────────────────────────────

void testSpecDefaultsMatchParameters()
{
    const Parameters p;
    assert(kParameterSpecs.size() == kParameterCount);
    assert(std::fabs(p.cylinders - kParameterSpecs[0].defaultValue) < 1.0e-6f);
    assert(std::fabs(p.displacement - kParameterSpecs[1].defaultValue) < 1.0e-6f);
    assert(std::fabs(p.rpm - kParameterSpecs[16].defaultValue) < 1.0e-6f);
    assert(std::fabs(p.throttle - kParameterSpecs[17].defaultValue) < 1.0e-6f);
    assert(std::fabs(p.level - kParameterSpecs[26].defaultValue) < 1.0e-6f);
    assert(kParameterSpecs[27].output);
    assert(kParameterSpecs[28].output);
    std::puts("PASS: testSpecDefaultsMatchParameters");
}

void testCycleFunctionBoundaries()
{
    // Intake occupies the first quarter only.
    assert(intakeValve(0.0f) == 0.0f);
    assert(intakeValve(0.25f) == 0.0f);
    assert(intakeValve(0.30f) == 0.0f);
    assert(intakeValve(0.125f) > 0.99f);

    // Exhaust occupies the last quarter only.
    assert(exhaustValve(0.5f) == 0.0f);
    assert(exhaustValve(0.75f) == 0.0f);
    assert(exhaustValve(0.875f) > 0.99f);

    // Piston is at the top at the start of the cycle and twice per cycle.
    assert(std::fabs(pistonMotion(0.0f) - 1.0f) < 1.0e-4f);
    assert(std::fabs(pistonMotion(0.25f) + 1.0f) < 1.0e-4f);
    assert(std::fabs(pistonMotion(0.5f) - 1.0f) < 1.0e-4f);

    // Ignition sits in the power stroke, is positive, and is zero outside.
    const float t = 0.4f;
    assert(fuelIgnition(0.25f, t) == 0.0f);
    assert(fuelIgnition(0.5f, t) == 0.0f);
    assert(std::fabs(fuelIgnition(0.5f + 0.5f * t, t)) < 1.0e-3f);  // closes at the window edge
    assert(fuelIgnition(0.5f + 0.25f * t, t) > 0.99f);
    assert(fuelIgnition(0.99f, t) == 0.0f);

    std::puts("PASS: testCycleFunctionBoundaries");
}

void testPhaseOffsets()
{
    std::array<float, kMaxCylinders> offsets {};
    for (int n = 1; n <= kMaxCylinders; ++n)
    {
        cylinderPhaseOffsets(n, 0.0f, offsets);
        for (int k = 0; k < n; ++k)
        {
            const float expected = static_cast<float>(k) / static_cast<float>(n);
            assert(std::fabs(offsets[static_cast<std::size_t>(k)] - expected) < 1.0e-5f);
        }
    }

    cylinderPhaseOffsets(8, 1.0f, offsets);
    float largest = 0.0f;
    for (int k = 0; k < 8; ++k)
    {
        assert(offsets[static_cast<std::size_t>(k)] >= 0.0f && offsets[static_cast<std::size_t>(k)] < 1.0f);
        if (k > 0)
            assert(offsets[static_cast<std::size_t>(k)] > offsets[static_cast<std::size_t>(k - 1)]);
        largest = std::max(largest, std::fabs(offsets[static_cast<std::size_t>(k)]
                                              - static_cast<float>(k) / 8.0f));
    }
    assert(largest > 0.02f);

    std::puts("PASS: testPhaseOffsets");
}

void testPhasorFrequencyMatchesRpm()
{
    auto state = makeEngine();
    Parameters p;
    p.rpm = 1800.0f;
    p.inertia = 10.0f;
    p.throttle = 0.5f;
    p = clampParameters(p);

    const TransportSnapshot transport;
    render(*state, p, transport, static_cast<std::size_t>(kSampleRate));  // settle
    assert(std::fabs(currentRpm(*state) - 1800.0f) < 5.0f);

    const std::uint32_t before = state->cycleCount;
    render(*state, p, transport, static_cast<std::size_t>(2.0 * kSampleRate));
    const std::uint32_t cycles = state->cycleCount - before;

    // 1800 rpm is 15 engine cycles per second.
    assert(cycles >= 29 && cycles <= 31);
    std::puts("PASS: testPhasorFrequencyMatchesRpm");
}

void testDefaultParametersProduceAudio()
{
    auto state = makeEngine();
    Parameters p = clampParameters(Parameters {});
    const TransportSnapshot transport;
    const auto out = render(*state, p, transport, 8192);
    assert(rms(out) > 1.0e-6);
    assert(allFinite(out, 2.0f));
    std::puts("PASS: testDefaultParametersProduceAudio");
}

void testFiringRateTracksCylinderCount()
{
    const auto firingPeak = [](const int cylinders) {
        auto state = makeEngine();
        Parameters p;
        p.cylinders = static_cast<float>(cylinders);
        p.rpm = 1200.0f;  // 10 engine cycles per second
        p.inertia = 10.0f;
        p.throttle = 0.8f;
        p.asymmetry = 0.0f;
        p.listen = 2.0f;  // rear: exhaust dominant
        p = clampParameters(p);

        const TransportSnapshot transport;
        render(*state, p, transport, static_cast<std::size_t>(kSampleRate));  // settle
        return render(*state, p, transport, static_cast<std::size_t>(2.0 * kSampleRate));
    };

    for (const int cylinders : {4, 6, 8})
    {
        const auto out = firingPeak(cylinders);
        const double firing = 10.0 * static_cast<double>(cylinders);
        const double atFiring = goertzel(out, firing, kSampleRate);
        assert(atFiring > 0.0);
        // The firing rate should dominate its immediate neighbourhood.
        assert(atFiring > 2.0 * goertzel(out, firing + 5.0, kSampleRate));
        assert(atFiring > 2.0 * goertzel(out, firing - 5.0, kSampleRate));
    }

    std::puts("PASS: testFiringRateTracksCylinderCount");
}

void testGrowlChangesOutput()
{
    const auto run = [](const float asymmetry) {
        auto state = makeEngine();
        Parameters p;
        p.cylinders = 8.0f;
        p.rpm = 1500.0f;
        p.inertia = 10.0f;
        p.throttle = 0.7f;
        p.asymmetry = asymmetry;
        p = clampParameters(p);
        const TransportSnapshot transport;
        return render(*state, p, transport, static_cast<std::size_t>(kSampleRate));
    };

    const auto flat = run(0.0f);
    const auto growl = run(0.9f);
    double difference = 0.0;
    for (std::size_t i = 0; i < flat.size(); ++i)
        difference = std::max(difference, std::fabs(static_cast<double>(flat[i] - growl[i])));
    assert(difference > 1.0e-4);
    std::puts("PASS: testGrowlChangesOutput");
}

void testOutputFiniteUnderExtremes()
{
    // Every parameter driven to each end of its range against a stressed rest.
    for (std::size_t index = 0; index < kInputParameterCount; ++index)
    {
        for (int end = 0; end < 2; ++end)
        {
            auto state = makeEngine();
            Parameters p;
            p.cylinders = 12.0f;
            p.throttle = 1.0f;
            p.compression = 14.0f;
            p.displacement = 100.0f;
            p.mufflerAction = 1.0f;
            p.backfire = 1.0f;
            p.level = 1.0f;

            const ParamSpec& s = kParameterSpecs[index];
            const float value = (end == 0) ? s.minimum : s.maximum;
            switch (static_cast<ParamId>(index))
            {
            case ParamId::cylinders: p.cylinders = value; break;
            case ParamId::displacement: p.displacement = value; break;
            case ParamId::compression: p.compression = value; break;
            case ParamId::ignition: p.ignition = value; break;
            case ParamId::asymmetry: p.asymmetry = value; break;
            case ParamId::blockGain: p.blockGain = value; break;
            case ParamId::intakeLen: p.intakeLen = value; break;
            case ParamId::intakeGain: p.intakeGain = value; break;
            case ParamId::turbulence: p.turbulence = value; break;
            case ParamId::extractorLen: p.extractorLen = value; break;
            case ParamId::pipeLen: p.pipeLen = value; break;
            case ParamId::mufflerLen: p.mufflerLen = value; break;
            case ParamId::mufflerAction: p.mufflerAction = value; break;
            case ParamId::outletLen: p.outletLen = value; break;
            case ParamId::outletGain: p.outletGain = value; break;
            case ParamId::backfire: p.backfire = value; break;
            case ParamId::rpm: p.rpm = value; break;
            case ParamId::throttle: p.throttle = value; break;
            case ParamId::rpmSource: p.rpmSource = value; break;
            case ParamId::syncRatio: p.syncRatio = value; break;
            case ParamId::idleRpm: p.idleRpm = value; break;
            case ParamId::inertia: p.inertia = value; break;
            case ParamId::seed: p.seed = value; break;
            case ParamId::midiCh: p.midiCh = value; break;
            case ParamId::listen: p.listen = value; break;
            case ParamId::width: p.width = value; break;
            case ParamId::level: p.level = value; break;
            default: break;
            }

            p = clampParameters(p);
            const TransportSnapshot transport;
            // 0.4 s is long enough for the slowest geometry smoother to settle
            // and for several engine cycles at the minimum speed.
            const auto out = render(*state, p, transport, static_cast<std::size_t>(0.4 * kSampleRate));
            assert(allFinite(out, 2.0f));
            assert(state->nonFinite == 0);
        }
    }
    std::puts("PASS: testOutputFiniteUnderExtremes");
}

void testOutputFiniteUnderRapidModulation()
{
    auto state = makeEngine();
    Parameters p = clampParameters(Parameters {});
    const TransportSnapshot transport;

    const std::size_t blocks = 2000;
    const std::size_t blockSize = 64;
    std::vector<float> left(blockSize, 0.0f);
    std::vector<float> right(blockSize, 0.0f);

    for (std::size_t b = 0; b < blocks; ++b)
    {
        const bool flip = (b & 1) != 0;
        p.rpm = flip ? 9000.0f : 400.0f;
        p.throttle = flip ? 1.0f : 0.0f;
        p.cylinders = flip ? 12.0f : 1.0f;
        p.pipeLen = flip ? 4.0f : 0.2f;
        p.displacement = flip ? 1200.0f : 100.0f;
        p.compression = flip ? 14.0f : 6.0f;
        p.mufflerLen = flip ? 1.5f : 0.1f;
        p.inertia = 10.0f;
        p = clampParameters(p);

        processBlock(*state, p, transport, static_cast<std::uint32_t>(blockSize), kSampleRate,
                     left.data(), right.data());
        assert(allFinite(left, 2.0f));
        assert(allFinite(right, 2.0f));
    }
    assert(state->nonFinite == 0);
    std::puts("PASS: testOutputFiniteUnderRapidModulation");
}

void testEngineDecaysWhenSilenced()
{
    auto state = makeEngine();
    Parameters p;
    p.throttle = 1.0f;
    p.rpm = 4000.0f;
    p.inertia = 10.0f;
    p.turbulence = 0.0f;
    p = clampParameters(p);
    const TransportSnapshot transport;

    render(*state, p, transport, static_cast<std::size_t>(kSampleRate));

    // Stop firing and let the resonators ring down.
    p.throttle = 0.0f;
    p.rpm = 400.0f;
    p.ignition = 0.02f;
    p = clampParameters(p);

    const auto early = render(*state, p, transport, 8192);
    render(*state, p, transport, static_cast<std::size_t>(2.0 * kSampleRate));
    const auto late = render(*state, p, transport, 8192);

    assert(rms(late) < rms(early));
    std::puts("PASS: testEngineDecaysWhenSilenced");
}

void testBackfireOnlyWhenDecelerating()
{
    const TransportSnapshot transport;

    // Steady speed: the Poisson test is never reached.
    {
        auto state = makeEngine();
        Parameters p;
        p.rpm = 3000.0f;
        p.throttle = 0.5f;
        p.backfire = 1.0f;
        p.inertia = 10.0f;
        p = clampParameters(p);
        render(*state, p, transport, static_cast<std::size_t>(2.0 * kSampleRate));
        const std::uint32_t settled = state->backfireCount;
        render(*state, p, transport, static_cast<std::size_t>(5.0 * kSampleRate));
        assert(state->backfireCount == settled);
    }

    // Deceleration with backfire disabled stays silent.
    {
        auto state = makeEngine();
        Parameters p;
        p.rpm = 6000.0f;
        p.throttle = 0.5f;
        p.backfire = 0.0f;
        p.inertia = 400.0f;
        p = clampParameters(p);
        render(*state, p, transport, static_cast<std::size_t>(kSampleRate));
        p.rpm = 800.0f;
        p = clampParameters(p);
        render(*state, p, transport, static_cast<std::size_t>(3.0 * kSampleRate));
        assert(state->backfireCount == 0);
    }

    // Deceleration with backfire enabled fires, deterministically, and the
    // repeat probability decays so it cannot run away.
    const auto decelerate = [&](const float seed) {
        auto state = makeEngine();
        Parameters p;
        p.rpm = 6000.0f;
        p.throttle = 0.8f;
        p.backfire = 0.9f;
        p.inertia = 600.0f;
        p.seed = seed;
        p = clampParameters(p);
        render(*state, p, transport, static_cast<std::size_t>(kSampleRate));
        p.rpm = 700.0f;
        p = clampParameters(p);
        const auto out = render(*state, p, transport, static_cast<std::size_t>(4.0 * kSampleRate));
        return std::make_pair(state->backfireCount, out);
    };

    const auto first = decelerate(1.0f);
    const auto repeat = decelerate(1.0f);
    assert(first.first > 0);
    assert(first.first <= 12);
    assert(first.first == repeat.first);
    assert(std::memcmp(first.second.data(), repeat.second.data(), first.second.size() * sizeof(float)) == 0);

    std::puts("PASS: testBackfireOnlyWhenDecelerating");
}

void testTransportSyncFrequency()
{
    auto state = makeEngine();
    Parameters p;
    p.rpmSource = 1.0f;
    p.syncRatio = 3.0f;  // 4 engine cycles per beat
    p.throttle = 0.5f;
    p.inertia = 10.0f;
    p = clampParameters(p);

    TransportSnapshot transport;
    transport.valid = true;
    transport.playing = true;
    transport.bpm = 120.0;

    render(*state, p, transport, static_cast<std::size_t>(kSampleRate));
    // 120 bpm x 4 cycles per beat = 8 Hz = 960 rpm.
    assert(std::fabs(currentRpm(*state) - 960.0f) < 3.0f);

    const std::uint32_t before = state->cycleCount;
    render(*state, p, transport, static_cast<std::size_t>(2.0 * kSampleRate));
    const std::uint32_t cycles = state->cycleCount - before;
    assert(cycles >= 15 && cycles <= 17);

    // A different tempo tracks proportionally.
    transport.bpm = 90.0;
    render(*state, p, transport, static_cast<std::size_t>(kSampleRate));
    assert(std::fabs(currentRpm(*state) - 720.0f) < 3.0f);

    std::puts("PASS: testTransportSyncFrequency");
}

void testStoppedTransportHoldsIdle()
{
    Parameters p;
    p.rpmSource = 1.0f;
    p.syncRatio = 5.0f;
    p.idleRpm = 850.0f;
    p.throttle = 0.3f;
    p.inertia = 50.0f;
    p = clampParameters(p);

    for (int variant = 0; variant < 2; ++variant)
    {
        auto state = makeEngine();
        TransportSnapshot transport;
        transport.valid = (variant == 0);
        transport.playing = false;
        transport.bpm = 120.0;

        render(*state, p, transport, static_cast<std::size_t>(2.0 * kSampleRate));
        assert(std::fabs(currentRpm(*state) - 850.0f) < 3.0f);
    }

    std::puts("PASS: testStoppedTransportHoldsIdle");
}

void testClampParametersEnforcesRange()
{
    Parameters wild;
    wild.cylinders = 99.0f;
    wild.displacement = -10.0f;
    wild.compression = 1000.0f;
    wild.ignition = 0.0f;
    wild.rpm = 1.0e9f;
    wild.seed = 0.0f;
    wild.listen = 9.0f;
    wild.syncRatio = -4.0f;
    wild.midiCh = 40.0f;
    wild.level = 2.0f;

    const Parameters clamped = clampParameters(wild);
    assert(clamped.cylinders == 12.0f);
    assert(clamped.displacement == kParameterSpecs[1].minimum);
    assert(clamped.compression == kParameterSpecs[2].maximum);
    assert(clamped.ignition == kParameterSpecs[3].minimum);
    assert(clamped.rpm == kParameterSpecs[16].maximum);
    assert(clamped.seed == 1.0f);
    assert(clamped.listen == 3.0f);
    assert(clamped.syncRatio == 0.0f);
    assert(clamped.midiCh == 17.0f);
    assert(clamped.level == 1.0f);

    // Integer parameters snap, and clamping is idempotent.
    Parameters fractional;
    fractional.cylinders = 4.7f;
    const Parameters snapped = clampParameters(fractional);
    assert(snapped.cylinders == 5.0f);
    const Parameters twice = clampParameters(clamped);
    assert(std::memcmp(&twice, &clamped, sizeof(Parameters)) == 0);

    std::puts("PASS: testClampParametersEnforcesRange");
}

void testSerializeRoundtrip()
{
    Parameters p;
    p.cylinders = 6.0f;
    p.displacement = 812.0f;
    p.compression = 11.5f;
    p.ignition = 0.31f;
    p.asymmetry = 0.44f;
    p.blockGain = 0.21f;
    p.intakeLen = 0.77f;
    p.intakeGain = 0.33f;
    p.turbulence = 0.88f;
    p.extractorLen = 0.91f;
    p.pipeLen = 2.34f;
    p.mufflerLen = 1.11f;
    p.mufflerAction = 0.27f;
    p.outletLen = 0.44f;
    p.outletGain = 0.66f;
    p.backfire = 0.55f;
    p.rpm = 4321.0f;
    p.throttle = 0.62f;
    p.rpmSource = 1.0f;
    p.syncRatio = 5.0f;
    p.idleRpm = 1234.0f;
    p.inertia = 999.0f;
    p.seed = 4242.0f;
    p.midiCh = 9.0f;
    p.listen = 2.0f;
    p.width = 0.19f;
    p.level = 0.51f;
    p = clampParameters(p);

    const std::string text = serializeParameters(p);
    assert(text.rfind("version=1\n", 0) == 0);

    const auto loaded = deserializeParameters(text);
    assert(loaded.has_value());
    assert(std::memcmp(&(*loaded), &p, sizeof(Parameters)) == 0);

    // Empty state is the default patch; malformed state is rejected outright.
    const auto empty = deserializeParameters("");
    assert(empty.has_value());
    assert(!deserializeParameters("version=1\nbogus_key=1\n").has_value());
    assert(!deserializeParameters("no_equals_sign\n").has_value());
    assert(!deserializeParameters("version=1\nrpm=abc\n").has_value());

    // Out-of-range values are clamped on load rather than rejected.
    const auto hot = deserializeParameters("version=1\nrpm=99999\n");
    assert(hot.has_value());
    assert(hot->rpm == kParameterSpecs[16].maximum);

    std::puts("PASS: testSerializeRoundtrip");
}

void testDeterministicAcrossBlockSizes()
{
    Parameters p;
    p.cylinders = 6.0f;
    p.throttle = 0.7f;
    p.rpm = 2400.0f;
    p.backfire = 0.5f;
    p = clampParameters(p);
    const TransportSnapshot transport;

    const std::size_t frames = 8192;
    auto a = makeEngine();
    auto b = makeEngine();
    auto c = makeEngine();

    const auto one = render(*a, p, transport, frames, frames);
    const auto many = render(*b, p, transport, frames, 64);
    const auto odd = render(*c, p, transport, frames, 37);

    assert(std::memcmp(one.data(), many.data(), frames * sizeof(float)) == 0);
    assert(std::memcmp(one.data(), odd.data(), frames * sizeof(float)) == 0);
    std::puts("PASS: testDeterministicAcrossBlockSizes");
}

void testNoAllocationAfterActivate()
{
    static_assert(std::is_trivially_copyable_v<EngineState>);
    static_assert(sizeof(EngineState) < 512u * 1024u);

    auto state = makeEngine();
    Parameters p = clampParameters(Parameters {});
    const TransportSnapshot transport;

    std::vector<float> left(256, 0.0f);
    std::vector<float> right(256, 0.0f);

    gAllocations.store(0);
    gCountAllocations.store(true);
    for (int block = 0; block < 200; ++block)
    {
        p.cylinders = static_cast<float>(1 + (block % 12));
        p.rpm = 400.0f + static_cast<float>(block) * 40.0f;
        p.throttle = static_cast<float>(block % 7) / 6.0f;
        p = clampParameters(p);
        processBlock(*state, p, transport, 256, kSampleRate, left.data(), right.data());
    }
    gCountAllocations.store(false);

    assert(gAllocations.load() == 0);
    std::puts("PASS: testNoAllocationAfterActivate");
}

// Regression: the listening-position weights were once computed and then never
// applied, so all four vantages sounded identical. Levels, not just finiteness.
// A note sets crankshaft speed, transposed down two octaves.
void testNoteToRpm()
{
    const auto& rpmSpec = kParameterSpecs[static_cast<std::size_t>(ParamId::rpm)];

    // A note two octaves below its nominal pitch: cycle frequency is
    // noteHz / 4, and RPM is 120 x that.
    const auto expected = [](const int note) {
        return 120.0 * 440.0 * std::pow(2.0, (note - 24 - 69) / 12.0);
    };

    for (const int note : {24, 36, 48, 55, 60})
        assert(std::fabs(static_cast<double>(noteToRpm(note)) - expected(note)) < 1.0);

    // Concrete anchors: C1 idles, C2 cruises, C4 is at the redline.
    assert(std::fabs(noteToRpm(24) - 980.0f) < 5.0f);
    assert(std::fabs(noteToRpm(36) - 1961.0f) < 5.0f);
    assert(std::fabs(noteToRpm(60) - 7844.0f) < 5.0f);

    // Monotonic across the keyboard, and clamped rather than wild at the ends.
    for (int note = 1; note < 128; ++note)
        assert(noteToRpm(note) >= noteToRpm(note - 1));
    assert(noteToRpm(0) == rpmSpec.minimum);
    assert(noteToRpm(127) == rpmSpec.maximum);

    // The whole declared RPM range is reachable from the keyboard.
    assert(noteToRpm(12) > rpmSpec.minimum && noteToRpm(12) < 1000.0f);
    assert(noteToRpm(63) == rpmSpec.maximum);

    std::puts("PASS: testNoteToRpm");
}

// Velocity is engine load, on the same scale as CC 1.
void testVelocityToThrottle()
{
    const auto& spec = kParameterSpecs[static_cast<std::size_t>(ParamId::throttle)];

    assert(std::fabs(velocityToThrottle(0) - spec.minimum) < 1.0e-6f);
    assert(std::fabs(velocityToThrottle(127) - spec.maximum) < 1.0e-6f);
    assert(std::fabs(velocityToThrottle(64) - 64.0f / 127.0f) < 1.0e-4f);

    for (int velocity = 1; velocity <= 127; ++velocity)
        assert(velocityToThrottle(velocity) > velocityToThrottle(velocity - 1));

    // Out-of-range input is clamped, not wrapped.
    assert(velocityToThrottle(-5) == spec.minimum);
    assert(velocityToThrottle(200) == spec.maximum);

    // A note and CC 1 carrying the same value mean the same thing.
    for (const int value : {0, 32, 64, 100, 127})
    {
        assert(std::fabs(velocityToThrottle(value)
                         - controllerToParameter(ParamId::throttle, static_cast<std::uint8_t>(value)))
               < 1.0e-6f);
    }

    // Soft and hard notes render audibly different engines.
    const auto renderAt = [](const int velocity) {
        auto state = makeEngine();
        Parameters p;
        p.rpm = noteToRpm(36);
        p.throttle = velocityToThrottle(velocity);
        p.inertia = 10.0f;
        p.listen = 2.0f;
        p = clampParameters(p);
        const TransportSnapshot transport;
        render(*state, p, transport, static_cast<std::size_t>(0.5 * kSampleRate));
        return rms(render(*state, p, transport, static_cast<std::size_t>(0.5 * kSampleRate)));
    };
    assert(renderAt(110) > renderAt(30) * 1.5);

    std::puts("PASS: testVelocityToThrottle");
}

// Drift's four lanes emit CC 1, 2, 3 and 4 on channel 1 by default
// (plugins/drift/include/drift_core.hpp). Each must reach a parameter that
// audibly changes the engine.
void testDriftControllersReachSoundSettings()
{
    const std::array<std::pair<std::uint8_t, ParamId>, 4> driftDefaults = {{
        {1, ParamId::throttle},
        {2, ParamId::rpm},
        {3, ParamId::mufflerAction},
        {4, ParamId::asymmetry},
    }};

    for (const auto& [controller, expectedTarget] : driftDefaults)
    {
        ParamId target = ParamId::level;
        assert(controllerTarget(controller, target));
        assert(target == expectedTarget);

        // The full controller range spans the full parameter range.
        const auto& spec = kParameterSpecs[static_cast<std::size_t>(target)];
        assert(std::fabs(controllerToParameter(target, 0) - spec.minimum) < 1.0e-4f);
        assert(std::fabs(controllerToParameter(target, 127) - spec.maximum) < 1.0e-4f);
        const float middle = controllerToParameter(target, 64);
        assert(middle > spec.minimum && middle < spec.maximum);

        // None of the four is a read-only status parameter or a patch setting.
        assert(!spec.output);
        assert(target != ParamId::seed);
    }

    // Unmapped controllers are ignored rather than hitting parameter zero.
    ParamId unused = ParamId::level;
    assert(!controllerTarget(0, unused));
    assert(!controllerTarget(64, unused));
    assert(!controllerTarget(120, unused));

    // Each of the four moves the sound, not just a number.
    const auto renderWith = [](const std::uint8_t controller, const std::uint8_t value) {
        auto state = makeEngine();
        Parameters p;
        p.throttle = 0.5f;
        p.rpm = 2600.0f;
        p.inertia = 10.0f;
        p.listen = 2.0f;
        ParamId target = ParamId::level;
        const bool mapped = controllerTarget(controller, target);
        assert(mapped);
        switch (target)
        {
        case ParamId::throttle: p.throttle = controllerToParameter(target, value); break;
        case ParamId::rpm: p.rpm = controllerToParameter(target, value); break;
        case ParamId::mufflerAction: p.mufflerAction = controllerToParameter(target, value); break;
        case ParamId::asymmetry: p.asymmetry = controllerToParameter(target, value); break;
        default: assert(false); break;
        }
        p = clampParameters(p);
        const TransportSnapshot transport;
        render(*state, p, transport, static_cast<std::size_t>(0.5 * kSampleRate));
        return render(*state, p, transport, static_cast<std::size_t>(0.5 * kSampleRate));
    };

    for (const auto& [controller, expectedTarget] : driftDefaults)
    {
        (void) expectedTarget;
        const auto low = renderWith(controller, 16);
        const auto high = renderWith(controller, 112);
        double difference = 0.0;
        for (std::size_t i = 0; i < low.size(); ++i)
            difference = std::max(difference, std::fabs(static_cast<double>(low[i] - high[i])));
        assert(difference > 1.0e-3);
        assert(allFinite(low, 2.0f) && allFinite(high, 2.0f));
    }

    std::puts("PASS: testDriftControllersReachSoundSettings");
}

void testListeningPositionsDiffer()
{
    const auto atPosition = [](const int position, const float level) {
        auto state = makeEngine();
        Parameters p;
        p.throttle = 0.9f;
        p.rpm = 3000.0f;
        p.inertia = 10.0f;
        p.listen = static_cast<float>(position);
        p.level = level;
        p = clampParameters(p);
        const TransportSnapshot transport;
        render(*state, p, transport, static_cast<std::size_t>(kSampleRate));
        return render(*state, p, transport, static_cast<std::size_t>(kSampleRate));
    };

    std::array<std::vector<float>, 4> rendered {};
    std::array<double, 4> loudness {};
    std::array<double, 4> brightness {};
    for (int position = 0; position < 4; ++position)
    {
        const auto index = static_cast<std::size_t>(position);
        rendered[index] = atPosition(position, 0.75f);
        assert(allFinite(rendered[index], 2.0f));
        loudness[index] = rms(rendered[index]);
        brightness[index] = highFrequencyRms(rendered[index], kSampleRate) / loudness[index];
        assert(loudness[index] > 1.0e-4);
    }

    // No two vantages render the same signal.
    for (std::size_t a = 0; a < rendered.size(); ++a)
    {
        for (std::size_t b = a + 1; b < rendered.size(); ++b)
        {
            double difference = 0.0;
            for (std::size_t i = 0; i < rendered[a].size(); ++i)
                difference = std::max(difference, std::fabs(static_cast<double>(rendered[a][i] - rendered[b][i])));
            assert(difference > 1.0e-3);
        }
    }

    // Rear is exhaust-led, so louder and darker than the block-led Cabin;
    // Front is intake-led, so the brightest of the four.
    assert(loudness[2] > loudness[0]);
    assert(brightness[1] > brightness[2]);
    assert(brightness[1] >= *std::max_element(brightness.begin(), brightness.end()));

    // Output level scales the mix proportionally.
    const double full = rms(atPosition(0, 1.0f));
    const double half = rms(atPosition(0, 0.5f));
    assert(full > 0.0);
    assert(std::fabs(half / full - 0.5) < 0.05);

    std::puts("PASS: testListeningPositionsDiffer");
}

void testMufflerActionSilences()
{
    const auto run = [](const float action) {
        auto state = makeEngine();
        Parameters p;
        p.throttle = 0.9f;
        p.rpm = 3000.0f;
        p.inertia = 10.0f;
        p.listen = 2.0f;  // rear, exhaust dominant
        p.intakeGain = 0.0f;
        p.blockGain = 0.0f;
        p.mufflerAction = action;
        p = clampParameters(p);
        const TransportSnapshot transport;
        render(*state, p, transport, static_cast<std::size_t>(kSampleRate));
        return rms(render(*state, p, transport, static_cast<std::size_t>(kSampleRate)));
    };

    const double loud = run(0.0f);
    const double quiet = run(1.0f);
    assert(loud > 0.0);
    assert(quiet < loud * 0.25);

    // Muffler element delays stay pairwise coprime across the length range.
    for (float length = 0.1f; length <= 1.5f; length += 0.05f)
    {
        std::array<int, kMufflerElements> delays {};
        for (int m = 0; m < kMufflerElements; ++m)
        {
            delays[static_cast<std::size_t>(m)] = static_cast<int>(
                nearestPrimeDelay(tubeDelaySamples(length, static_cast<float>(kSampleRate))
                                  * kMufflerRatio[static_cast<std::size_t>(m)]));
        }
        for (int a = 0; a < kMufflerElements; ++a)
        {
            for (int b = a + 1; b < kMufflerElements; ++b)
            {
                const int x = delays[static_cast<std::size_t>(a)];
                const int y = delays[static_cast<std::size_t>(b)];
                assert(x == y || std::gcd(x, y) == 1);
            }
        }
    }

    std::puts("PASS: testMufflerActionSilences");
}

}  // namespace

void* operator new(const std::size_t size)
{
    if (gCountAllocations.load())
        gAllocations.fetch_add(1);
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr)
        throw std::bad_alloc();
    return memory;
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

int main()
{
    testSpecDefaultsMatchParameters();
    testCycleFunctionBoundaries();
    testPhaseOffsets();
    testPhasorFrequencyMatchesRpm();
    testDefaultParametersProduceAudio();
    testFiringRateTracksCylinderCount();
    testGrowlChangesOutput();
    testOutputFiniteUnderExtremes();
    testOutputFiniteUnderRapidModulation();
    testEngineDecaysWhenSilenced();
    testBackfireOnlyWhenDecelerating();
    testTransportSyncFrequency();
    testStoppedTransportHoldsIdle();
    testClampParametersEnforcesRange();
    testSerializeRoundtrip();
    testDeterministicAcrossBlockSizes();
    testNoAllocationAfterActivate();
    testNoteToRpm();
    testVelocityToThrottle();
    testDriftControllersReachSoundSettings();
    testListeningPositionsDiffer();
    testMufflerActionSilences();
    std::puts("All Magneto core tests passed.");
    return 0;
}
