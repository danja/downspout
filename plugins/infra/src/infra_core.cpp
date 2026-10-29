#include "infra_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::infra {

namespace {

constexpr double kPi = 3.14159265358979323846;

float clampf(float v, float lo, float hi) noexcept
{
    if (!std::isfinite(v)) return lo;
    return std::min(std::max(v, lo), hi);
}

// Deterministic value in -1..1 for a cycle number.
float hashCycle(std::int64_t n) noexcept
{
    std::uint64_t x = static_cast<std::uint64_t>(n) * 0x9E3779B97F4A7C15ull + 0x1234567ull;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<float>(static_cast<double>(x >> 11) / 4503599627370496.0 - 1.0);   // 53 bits -> [-1, 1)
}

double frac(double x) noexcept { return x - std::floor(x); }

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters o = p;
    o.waveform = std::round(clampf(p.waveform, 0.0f, static_cast<float>(kWaveformCount - 1)));
    o.multiple = std::round(clampf(p.multiple, 0.0f, static_cast<float>(kMaxMultiple)));
    o.phaseL   = std::round(clampf(p.phaseL, 0.0f, 3.0f));
    o.phaseR   = std::round(clampf(p.phaseR, 0.0f, 3.0f));
    o.rangeDb  = clampf(p.rangeDb, 0.0f, 40.0f);
    return o;
}

int effectiveMultiple(const float requested, const double bpmIn) noexcept
{
    const double beatHz = (bpmIn > 0.0 ? bpmIn : kFallbackBpm) / 60.0;
    // The multiples that keep the rate in 1..20 Hz (a tiny epsilon keeps exact edges, e.g. 20 Hz, inside).
    int lo = std::max(1, static_cast<int>(std::ceil(kMinRateHz / beatHz - 1e-9)));
    int hi = std::min(kMaxMultiple, static_cast<int>(std::floor(kMaxRateHz / beatHz + 1e-9)));
    if (hi < lo) hi = lo;   // a beat faster than 20 Hz: the slowest we can do is one cycle per beat
    int m = static_cast<int>(std::lround(requested));
    if (m <= 0) m = static_cast<int>(std::lround(kAutoTargetHz / beatHz));
    return std::clamp(m, lo, hi);
}

double rateHz(const int multiple, const double bpm) noexcept
{
    return static_cast<double>(multiple) * (bpm > 0.0 ? bpm : kFallbackBpm) / 60.0;
}

float lfoValue(const Waveform wave, const double cycles) noexcept
{
    const double ph = frac(cycles);
    const double s = std::sin(2.0 * kPi * ph);
    double b;   // bipolar -1..1, sine-aligned (the waveforms rise first and peak a quarter cycle in)
    switch (wave) {
    case Waveform::Triangle:   b = 2.0 / kPi * std::asin(s); break;
    case Waveform::RampUp:     b = 2.0 * ph - 1.0; break;
    case Waveform::RampDown:   b = 1.0 - 2.0 * ph; break;
    case Waveform::Square:     b = ph < 0.5 ? 1.0 : -1.0; break;
    case Waveform::SoftSquare: b = std::tanh(4.0 * s) / std::tanh(4.0); break;
    case Waveform::SampleHold: b = hashCycle(static_cast<std::int64_t>(std::floor(cycles))); break;
    case Waveform::SmoothRandom: {
        const std::int64_t n = static_cast<std::int64_t>(std::floor(cycles));
        const double t = 0.5 - 0.5 * std::cos(kPi * ph);
        b = hashCycle(n) * (1.0 - t) + hashCycle(n + 1) * t;
        break;
    }
    default:                   b = s; break;
    }
    return static_cast<float>(0.5 + 0.5 * std::clamp(b, -1.0, 1.0));
}

float gainDbFor(const float lfo, const float rangeDb) noexcept
{
    return -rangeDb * (1.0f - clampf(lfo, 0.0f, 1.0f));
}

void resetState(EngineState& state) noexcept
{
    state = EngineState {};
}

void processBlock(EngineState& state, const Parameters& rawParams, const float sampleRate, const uint32_t frames,
                  const Transport& transport, const float* const* inputs, float* const* outputs) noexcept
{
    const Parameters p = clampParameters(rawParams);
    const double sr = sampleRate > 0.0f ? sampleRate : 44100.0;
    const double bpm = transport.valid && transport.bpm > 0.0 ? transport.bpm : kFallbackBpm;
    const int multiple = effectiveMultiple(p.multiple, bpm);
    const double hz = rateHz(multiple, bpm);
    const bool locked = transport.playing && transport.valid;
    const Waveform wave = static_cast<Waveform>(static_cast<int>(p.waveform));
    const double offset[2] = {0.25 * p.phaseL, 0.25 * p.phaseR};

    // The gain moves in dB, smoothed over about half a millisecond so a square wave or a random step does not click.
    const float smooth = 1.0f - static_cast<float>(std::exp(-1.0 / (0.0005 * sr)));
    const double beatsPerSample = bpm / 60.0 / sr;
    const double cyclesPerSample = hz / sr;
    const double base = locked ? transport.beatPosition * multiple : state.freePhase;

    for (uint32_t i = 0; i < frames; ++i) {
        const double cycles = base + static_cast<double>(i) * (locked ? beatsPerSample * multiple : cyclesPerSample);
        for (int c = 0; c < 2; ++c) {
            const float lfo = lfoValue(wave, cycles + offset[c]);
            const float target = gainDbFor(lfo, p.rangeDb);
            float& g = state.smoothedDb[static_cast<size_t>(c)];
            if (!state.primed) g = target;
            g += (target - g) * smooth;
            const float in = inputs[c] ? inputs[c][i] : (inputs[0] ? inputs[0][i] : 0.0f);
            outputs[c][i] = (std::isfinite(in) ? in : 0.0f) * std::pow(10.0f, g * 0.05f);
            if (i + 1 == frames) state.meters.lfo[static_cast<size_t>(c)] = lfo;
        }
        state.primed = true;
    }
    state.freePhase += static_cast<double>(frames) * cyclesPerSample;   // unwrapped: the random waveforms need the cycle count
    state.meters.rateHz = static_cast<float>(hz);
    state.meters.multiple = static_cast<float>(multiple);
}

}  // namespace downspout::infra
