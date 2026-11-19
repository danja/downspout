#include "splay_core_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace downspout::splay {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct MatrixGains {
    float ll;
    float lr;
    float rl;
    float rr;
};

struct Stereo {
    float l;
    float r;
};

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

// Constant-power compensation only when a row or column has more than one
// active path. A single boosted path is left alone, so the switch never
// silently cancels a deliberate gain boost.
[[nodiscard]] MatrixGains constantPower(const MatrixGains& g) noexcept
{
    const float rowL = (g.ll > 0.0f && g.rl > 0.0f) ? std::sqrt(g.ll * g.ll + g.rl * g.rl) : 1.0f;
    const float rowR = (g.lr > 0.0f && g.rr > 0.0f) ? std::sqrt(g.lr * g.lr + g.rr * g.rr) : 1.0f;
    const float colL = (g.ll > 0.0f && g.lr > 0.0f) ? std::sqrt(g.ll * g.ll + g.lr * g.lr) : 1.0f;
    const float colR = (g.rl > 0.0f && g.rr > 0.0f) ? std::sqrt(g.rl * g.rl + g.rr * g.rr) : 1.0f;
    return {
        g.ll / std::max({1.0f, rowL, colL}),
        g.lr / std::max({1.0f, rowR, colL}),
        g.rl / std::max({1.0f, rowL, colR}),
        g.rr / std::max({1.0f, rowR, colR}),
    };
}

[[nodiscard]] Stereo midSide(const float l, const float r, const float midGain,
                             const float sideGain, const float solo) noexcept
{
    const float m = 0.5f * (l + r) * midGain;
    const float s = 0.5f * (l - r) * sideGain;
    const float normalL = m + s;
    const float normalR = m - s;

    if (solo <= 0.0f)
        return {normalL, normalR};
    if (solo >= 2.0f)
        return {s, -s};
    if (solo <= 1.0f)
        return {approach(normalL, m, solo), approach(normalR, m, solo)};

    const float w = solo - 1.0f;
    return {approach(m, s, w), approach(m, -s, w)};
}

}  // namespace

float ccToValue(const int slot, const int cc) noexcept
{
    const float u = static_cast<float>(std::clamp(cc, 0, 127)) / 127.0f;
    switch (slot) {
    case 0: return -90.0f + 180.0f * u;  // Rotation
    case 1: return 2.0f * u;              // Width
    case 2: return 2.0f * u;              // Mid
    case 3: return 20.0f * u;             // Image Delay
    default: return 0.0f;
    }
}

bool ccChannelMatches(const unsigned char status, const float ccChannel) noexcept
{
    return (status & 0xF0) == 0xB0 && ccChannel >= 0.5f &&
           static_cast<int>(status & 0x0F) + 1 == static_cast<int>(std::lround(ccChannel));
}

void handleController(LiveControl& live, const int controller, const int value) noexcept
{
    if (controller >= 1 && controller <= kCcSlots) {
        const std::size_t slot = static_cast<std::size_t>(controller - 1);
        live.value[slot] = ccToValue(slot, value);
        live.active[slot] = true;
    }
}

Parameters withLive(const Parameters& p, const LiveControl& live) noexcept
{
    Parameters effective = p;
    if (live.active[0]) effective.rotation = live.value[0];
    if (live.active[1]) effective.width = live.value[1];
    if (live.active[2]) effective.midGain = live.value[2];
    if (live.active[3]) effective.imageDelay = live.value[3];
    return effective;
}

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
    state.lowL = 0.0f;
    state.lowR = 0.0f;
    state.corr = 0.0f;
    state.mono = 0.0f;
}

void processBlock(EngineState& state, const Parameters& params, const std::uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept
{
    if (frames == 0)
        return;

    const Parameters p = clampParameters(params);
    const float coefficient =
        1.0f - std::exp(-1.0f / (static_cast<float>(kSmoothMs * 0.001 * state.sampleRate)));
    const float meterCoefficient =
        1.0f - std::exp(-1.0f / (static_cast<float>(kMeterMs * 0.001 * state.sampleRate)));
    const float sampleRate = static_cast<float>(state.sampleRate);
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

        state.rotation = p.rotation;
        state.panLaw = p.panLaw;
        state.midGain = p.midGain;
        state.solo = p.msSolo;
        state.spectral = p.spectralWidth;
        state.crossover = p.crossover;
        state.lowWidth = p.lowWidth;
        state.highWidth = p.highWidth;
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

        state.rotation = approach(state.rotation, p.rotation, coefficient);
        state.panLaw = approach(state.panLaw, p.panLaw, coefficient);
        state.midGain = approach(state.midGain, p.midGain, coefficient);
        state.solo = approach(state.solo, p.msSolo, coefficient);
        state.spectral = approach(state.spectral, p.spectralWidth, coefficient);
        state.crossover = approach(state.crossover, p.crossover, coefficient);
        state.lowWidth = approach(state.lowWidth, p.lowWidth, coefficient);
        state.highWidth = approach(state.highWidth, p.highWidth, coefficient);

        const float inL = inputs != nullptr && inputs[0] != nullptr ? sanitise(inputs[0][n], state.faults) : 0.0f;
        const float inR = inputs != nullptr && inputs[1] != nullptr ? sanitise(inputs[1][n], state.faults) : 0.0f;

        const MatrixGains raw {state.ll, state.lr, state.rl, state.rr};
        const MatrixGains even = constantPower(raw);
        const float mix = state.panLaw;
        const MatrixGains g {
            approach(raw.ll, even.ll, mix),
            approach(raw.lr, even.lr, mix),
            approach(raw.rl, even.rl, mix),
            approach(raw.rr, even.rr, mix),
        };

        float l = inL * g.ll + inR * g.rl;
        float r = inL * g.lr + inR * g.rr;

        // Stereo rotation, with 0 degrees exactly transparent.
        const float radians = state.rotation * kPi / 180.0f;
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        const float rotL = l * c - r * s;
        const float rotR = l * s + r * c;

        const Stereo plain = midSide(rotL, rotR, state.midGain, state.width, state.solo);
        Stereo shaped = plain;

        if (state.spectral > 0.0f) {
            const float cutoff = std::min(state.crossover, 0.45f * sampleRate);
            const float alpha = 1.0f - std::exp(-2.0f * kPi * cutoff / sampleRate);
            state.lowL += alpha * (rotL - state.lowL);
            state.lowR += alpha * (rotR - state.lowR);
            const float highL = rotL - state.lowL;
            const float highR = rotR - state.lowR;

            const Stereo low = midSide(state.lowL, state.lowR, state.midGain,
                                       state.width * state.lowWidth, state.solo);
            const Stereo high = midSide(highL, highR, state.midGain,
                                        state.width * state.highWidth, state.solo);
            const float w = state.spectral;
            shaped = {plain.l + (low.l + high.l - plain.l) * w,
                      plain.r + (low.r + high.r - plain.r) * w};
        }

        l = shaped.l;
        r = shaped.r;

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

        const float outL = flushDenormal(l);
        const float outR = flushDenormal(r);
        if (outputs != nullptr && outputs[0] != nullptr)
            outputs[0][n] = outL;
        if (outputs != nullptr && outputs[1] != nullptr)
            outputs[1][n] = outR;

        // Meters read the final outputs, so bypass and inversion are reflected.
        const float mid = 0.5f * (outL + outR);
        const float side = 0.5f * (outL - outR);
        const float energy = mid * mid + side * side;
        const float instantCorr = energy > 0.0f ? (mid * mid - side * side) / energy : 0.0f;
        const float magnitude = std::fabs(outL) + std::fabs(outR);
        const float instantMono = magnitude > 0.0f ? 2.0f * std::fabs(mid) / magnitude : 0.0f;
        state.corr = approach(state.corr, std::clamp(instantCorr, -1.0f, 1.0f), meterCoefficient);
        state.mono = approach(state.mono, std::clamp(instantMono, 0.0f, 1.0f), meterCoefficient);
    }
}

}  // namespace downspout::splay
