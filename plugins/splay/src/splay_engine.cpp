#include "splay_core_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace downspout::splay {
namespace {

[[nodiscard]] float sanitise(const float value, std::size_t& faults) noexcept
{
    if (std::isfinite(value))
        return value;
    ++faults;
    return 0.0f;
}

[[nodiscard]] float flushDenormal(const float value) noexcept
{
    return std::fabs(value) < 1.0e-20f ? 0.0f : value;
}

[[nodiscard]] float approach(const float current, const float target, const float coefficient) noexcept
{
    return current + (target - current) * coefficient;
}

}  // namespace

void prepare(EngineState& state, const double sampleRate) noexcept
{
    state.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset(state);
}

void reset(EngineState& state) noexcept
{
    state.line.fill(0.0f);
    state.write = 0;
    state.faults = 0;
    state.primed = false;
}

void processBlock(EngineState& state, const Parameters& params, const std::uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept
{
    if (frames == 0)
        return;

    const Parameters p = clampParameters(params);
    const float coefficient = 1.0f - std::exp(-1.0f / (static_cast<float>(kSmoothMs * 0.001 * state.sampleRate)));
    const float maxDelay = static_cast<float>(kMaxDelaySamples - 2);
    const float targetDelay = std::clamp(static_cast<float>(p.imageDelay * state.sampleRate / 1000.0),
                                         0.0f, maxDelay);
    const float targetInvert = p.invertR > 0.5f ? -1.0f : 1.0f;
    const float targetDry = p.bypass > 0.5f ? 1.0f : 0.0f;

    if (!state.primed) {
        state.ll = p.matrixLL;
        state.lr = p.matrixLR;
        state.rl = p.matrixRL;
        state.rr = p.matrixRR;
        state.width = p.width;
        state.delay = targetDelay;
        state.invert = targetInvert;
        state.level = p.level;
        state.dry = targetDry;
        state.primed = true;
    }

    for (std::uint32_t n = 0; n < frames; ++n) {
        state.ll = approach(state.ll, p.matrixLL, coefficient);
        state.lr = approach(state.lr, p.matrixLR, coefficient);
        state.rl = approach(state.rl, p.matrixRL, coefficient);
        state.rr = approach(state.rr, p.matrixRR, coefficient);
        state.width = approach(state.width, p.width, coefficient);
        state.delay = approach(state.delay, targetDelay, coefficient);
        state.invert = approach(state.invert, targetInvert, coefficient);
        state.level = approach(state.level, p.level, coefficient);
        state.dry = approach(state.dry, targetDry, coefficient);

        const float inL = inputs != nullptr && inputs[0] != nullptr ? sanitise(inputs[0][n], state.faults) : 0.0f;
        const float inR = inputs != nullptr && inputs[1] != nullptr ? sanitise(inputs[1][n], state.faults) : 0.0f;

        // Four-way routing, then mid/side width.
        float l = inL * state.ll + inR * state.rl;
        float r = inL * state.lr + inR * state.rr;
        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * state.width;
        l = mid + side;
        r = mid - side;

        // Fractional inter-channel delay, applied to the right channel only.
        state.line[state.write] = r;
        const std::uint32_t whole = static_cast<std::uint32_t>(state.delay);
        const float fraction = state.delay - static_cast<float>(whole);
        const std::size_t newer = (state.write + kMaxDelaySamples - whole) % kMaxDelaySamples;
        const std::size_t older = (newer + kMaxDelaySamples - 1) % kMaxDelaySamples;
        r = state.line[newer] * (1.0f - fraction) + state.line[older] * fraction;
        state.write = (state.write + 1) % kMaxDelaySamples;

        r *= state.invert;
        l *= state.level;
        r *= state.level;

        // Bypass is a crossfade, not a hard switch, so toggling it does not click.
        l = l * (1.0f - state.dry) + inL * state.dry;
        r = r * (1.0f - state.dry) + inR * state.dry;

        if (outputs != nullptr && outputs[0] != nullptr)
            outputs[0][n] = flushDenormal(l);
        if (outputs != nullptr && outputs[1] != nullptr)
            outputs[1][n] = flushDenormal(r);
    }
}

}  // namespace downspout::splay
