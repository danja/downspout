#include "guardian_core.hpp"
#include "downspout/test_assert.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace downspout::guardian;

static std::array<float, kParameterCount> defaultParams()
{
    std::array<float, kParameterCount> p{};
    for (std::size_t i = 0; i < p.size(); ++i)
        p[i] = kParameterSpecs[i].defaultValue;
    return p;
}

static std::array<float, kParameterCount> legacyParams()
{
    auto p = defaultParams();
    p[kInterpolate] = 0; // isolate the original limiter path
    p[kDcRemove] = 0;
    p[kTruePeak] = 0;
    return p;
}

static float ceilingOf(const std::array<float, kParameterCount>& p)
{
    return std::pow(10.0f, p[kCeilingDb] / 20.0f);
}

int main()
{
    auto p = defaultParams();
    assert(p[kInterpolate] == 1.0f);
    assert(lookaheadFrames(p, 48000) == 240);
    assert(interpFrames(48000) == 240);

    // Hot input is limited and density reports every protected sample.
    auto hot = legacyParams();
    State s;
    prepare(s, 48000);
    std::array<float, 1024> input{}, left{}, right{};
    input.fill(3.0f);
    process(s, hot, input.size(), 48000, input.data(), input.data(), left.data(), right.data());
    for (float v : left) assert(std::isfinite(v) && std::fabs(v) <= ceilingOf(hot) + 1e-3f);
    assert(s.density > 99.0f);

    // Non-finite input is sanitised without corrupting the output.
    input.fill(0);
    input[0] = std::numeric_limits<float>::quiet_NaN();
    process(s, hot, input.size(), 48000, input.data(), input.data(), left.data(), right.data());
    for (float v : left) assert(std::isfinite(v));

    // Bypass passes audio unchanged and reports no activity.
    auto bypass = defaultParams();
    bypass[kBypass] = 1;
    input.fill(0.5f);
    prepare(s, 48000);
    process(s, bypass, input.size(), 48000, input.data(), input.data(), left.data(), right.data());
    for (float v : left) assert(std::fabs(v - 0.5f) < 1e-6f);
    assert(s.density == 0.0f);
    assert(std::fabs(s.inputPeak - 0.5f) < 1e-6f);

    // Input gain: +6 dB doubles amplitude below the ceiling.
    auto gain = legacyParams();
    gain[kInputDb] = 6.0f;
    gain[kLookaheadMs] = 0;
    input.fill(0.1f);
    prepare(s, 48000);
    process(s, gain, 1, 48000, input.data(), input.data(), left.data(), right.data());
    assert(std::fabs(left[0] - 0.1f * std::pow(10.0f, 6.0f / 20.0f)) < 0.01f);
    assert(std::fabs(s.inputPeak - 0.1f) < 1e-6f); // marker uses pre-gain peak

    // Instant attack keeps the first hot sample at the ceiling.
    auto instant = legacyParams();
    instant[kLookaheadMs] = 0;
    input.fill(3.0f);
    prepare(s, 48000);
    process(s, instant, 1, 48000, input.data(), input.data(), left.data(), right.data());
    assert(std::fabs(left[0]) <= ceilingOf(instant) + 1e-3f);

    // Clip interpolation: a hot run becomes a straight ramp between the clean
    // samples on either side, while every clean sample passes through exactly.
    auto interp = defaultParams();
    interp[kDcRemove] = 0;
    interp[kTruePeak] = 0;
    interp[kLookaheadMs] = 0;
    input.fill(-0.1f);
    for (std::size_t i = 0; i < 500; ++i) input[i] = 0.3f;
    for (std::size_t i = 500; i < 508; ++i) input[i] = 2.0f;
    std::array<float, 1024> interpolated{}, hardClipped{};
    prepare(s, 48000);
    process(s, interp, input.size(), 48000, input.data(), input.data(), interpolated.data(), right.data());
    const std::uint32_t window = interpFrames(48000);
    for (std::size_t i = window; i < 500 + window; ++i)
        assert(std::fabs(interpolated[i] - 0.3f) < 1e-6f);
    for (std::size_t j = 0; j < 8; ++j) {
        const float expected = 0.3f + (-0.1f - 0.3f) * (j + 1.0f) / 9.0f;
        assert(std::fabs(interpolated[window + 500 + j] - expected) < 1e-5f);
    }
    for (std::size_t i = 508 + window; i < input.size(); ++i)
        assert(std::fabs(interpolated[i] + 0.1f) < 1e-6f);
    assert(s.density > 0.7f && s.density < 0.9f);

    // With interpolation off the same run is flat-topped instead.
    auto noInterp = interp;
    noInterp[kInterpolate] = 0;
    prepare(s, 48000);
    process(s, noInterp, input.size(), 48000, input.data(), input.data(), hardClipped.data(), right.data());
    for (std::size_t j = 0; j < 8; ++j)
        assert(hardClipped[500 + j] > 0.8f);
    assert(std::fabs(hardClipped[500] - interpolated[window + 500]) > 0.5f);

    // A run longer than the interpolation window stays bounded by the ceiling.
    input.fill(2.0f);
    prepare(s, 48000);
    process(s, interp, input.size(), 48000, input.data(), input.data(), left.data(), right.data());
    for (float v : left) assert(std::isfinite(v) && std::fabs(v) <= ceilingOf(interp) + 1e-3f);

    // 4x detector catches an inter-sample peak between two legal samples.
    std::array<float, 64> odd{};
    for (std::size_t i = 0; i < odd.size(); ++i) {
        const float cycle[4] = {-0.85f, 0.85f, 0.85f, -0.85f};
        odd[i] = cycle[i % 4];
    }
    auto peakGuard = legacyParams();
    prepare(s, 48000);
    process(s, peakGuard, odd.size(), 48000, odd.data(), odd.data(), left.data(), right.data());
    assert(s.density == 0.0f);
    peakGuard[kTruePeak] = 1;
    prepare(s, 48000);
    process(s, peakGuard, odd.size(), 48000, odd.data(), odd.data(), left.data(), right.data());
    assert(s.density > 0.0f);

    // Program release recovers faster after a short peak.
    std::array<float, 128> transient{};
    for (std::size_t i = 0; i < 4; ++i) transient[i] = 3.0f;
    for (std::size_t i = 4; i < transient.size(); ++i) transient[i] = 0.1f;
    auto normalRelease = legacyParams();
    auto programRelease = normalRelease;
    programRelease[kProgramRelease] = 1;
    State sNormal, sProgram;
    prepare(sNormal, 48000);
    prepare(sProgram, 48000);
    process(sNormal, normalRelease, transient.size(), 48000, transient.data(), transient.data(), left.data(), right.data());
    process(sProgram, programRelease, transient.size(), 48000, transient.data(), transient.data(), left.data(), right.data());
    assert(sProgram.gain > sNormal.gain + 0.01f);

    // Auto drive trims gain until the requested peak reduction is reached.
    auto autoDrive = legacyParams();
    autoDrive[kAutoDrive] = 1;
    prepare(s, 48000);
    input.fill(3.0f);
    for (int block = 0; block < 400; ++block)
        process(s, autoDrive, 256, 48000, input.data(), input.data(), left.data(), right.data());
    assert(s.autoGainDb < -1.0f);
    assert(std::fabs(s.density - 100.0f) < 0.01f);

    // Disabling auto drive returns to the manual input gain immediately.
    autoDrive[kAutoDrive] = 0;
    process(s, autoDrive, 256, 48000, input.data(), input.data(), left.data(), right.data());
    assert(s.autoGainDb == 0.0f);

    // Switching interpolation off and back on clears its history.
    input.fill(0.1f);
    auto toggled = defaultParams();
    toggled[kDcRemove] = 0;
    toggled[kTruePeak] = 0;
    toggled[kLookaheadMs] = 0;
    prepare(s, 48000);
    toggled[kInterpolate] = 0;
    process(s, toggled, 600, 48000, input.data(), input.data(), left.data(), right.data());
    toggled[kInterpolate] = 1;
    process(s, toggled, 600, 48000, input.data(), input.data(), left.data(), right.data());
    for (std::size_t i = interpFrames(48000); i < 600; ++i)
        assert(std::fabs(left[i] - 0.1f) < 1e-6f);

    return 0;
}
