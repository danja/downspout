#include "trio_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::trio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kDbToNeper = 0.115129255f;  // ln(10) / 20
constexpr float kFloorLevel = 1.0e-6f;      // -120 dB

float clampf(float v, float lo, float hi) noexcept
{
    if (!std::isfinite(v)) return lo;
    return std::min(std::max(v, lo), hi);
}

Biquad butterworth(double fc, double sr, bool highPass) noexcept
{
    const double w0 = 2.0 * kPi * std::min(fc, sr * 0.45) / sr;
    const double cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * 0.70710678118654752);
    const double a0 = 1.0 + alpha;
    Biquad b;
    if (highPass) {
        b.b0 = (1.0 + cw) * 0.5 / a0; b.b1 = -(1.0 + cw) / a0; b.b2 = b.b0;
    } else {
        b.b0 = (1.0 - cw) * 0.5 / a0; b.b1 = (1.0 - cw) / a0; b.b2 = b.b0;
    }
    b.a1 = -2.0 * cw / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

inline double runCell(Cell& s, const Biquad& c, double x) noexcept
{
    const double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

inline double runLr4(Lr4& f, const Biquad& c, double x) noexcept
{
    return runCell(f.cell[1], c, runCell(f.cell[0], c, x));
}

inline float coefficient(float ms, float sr) noexcept
{
    return std::exp(-1.0f / (std::max(ms, 0.01f) * 0.001f * sr));
}

}  // namespace

Parameters::Parameters()
{
    // The canonical "OTT" settings: lower thresholds and slower times in the lows.
    bands[0] = {1.0f, -24.0f, 4.0f, -36.0f, 2.5f, 10.0f, 100.0f, 0.0f};
    bands[1] = {1.0f, -20.0f, 4.0f, -32.0f, 2.5f, 5.0f, 80.0f, 0.0f};
    bands[2] = {1.0f, -18.0f, 4.0f, -30.0f, 2.5f, 2.0f, 50.0f, 0.0f};
}

Band clampBand(const Band& in) noexcept
{
    Band b = in;
    b.on           = in.on >= 0.5f ? 1.0f : 0.0f;
    b.downThreshDb = clampf(in.downThreshDb, -60.0f, 0.0f);
    b.downRatio    = clampf(in.downRatio, 1.0f, 20.0f);
    b.upThreshDb   = clampf(in.upThreshDb, -80.0f, 0.0f);
    b.upRatio      = clampf(in.upRatio, 1.0f, 10.0f);
    b.attackMs     = clampf(in.attackMs, 0.5f, 200.0f);
    b.releaseMs    = clampf(in.releaseMs, 5.0f, 2000.0f);
    b.gainDb       = clampf(in.gainDb, -18.0f, 18.0f);
    return b;
}

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters o = p;
    o.inputDb     = clampf(p.inputDb, -12.0f, 24.0f);
    o.outputDb    = clampf(p.outputDb, -24.0f, 12.0f);
    o.amount      = clampf(p.amount, 0.0f, 100.0f);
    o.time        = clampf(p.time, 0.1f, 10.0f);
    o.upLimitDb   = clampf(p.upLimitDb, 0.0f, 24.0f);
    o.crossLowHz  = clampf(p.crossLowHz, 40.0f, 1000.0f);
    o.crossHighHz = clampf(p.crossHighHz, 1000.0f, 12000.0f);
    // Keep the two crossovers apart so the mid band cannot collapse.
    o.crossHighHz = std::max(o.crossHighHz, std::min(o.crossLowHz * 1.25f, 12000.0f));
    for (int i = 0; i < kBands; ++i) o.bands[static_cast<size_t>(i)] = clampBand(p.bands[static_cast<size_t>(i)]);
    return o;
}

float bandTargetDb(const Band& raw, float levelDb, float upLimitDb) noexcept
{
    const Band b = clampBand(raw);
    float target = 0.0f;
    if (levelDb > b.downThreshDb)
        target -= (levelDb - b.downThreshDb) * (1.0f - 1.0f / b.downRatio);
    // The lift threshold may not sit above the compress threshold, or the two would fight.
    const float upThresh = std::min(b.upThreshDb, b.downThreshDb);
    if (levelDb < upThresh)
        target += std::min((upThresh - levelDb) * (1.0f - 1.0f / b.upRatio), std::max(upLimitDb, 0.0f));
    return target;
}

void resetState(EngineState& state) noexcept
{
    state = EngineState {};
}

void processBlock(EngineState& state, const Parameters& rawParams, float sampleRate, uint32_t frames,
                  const float* const* inputs, float* const* outputs) noexcept
{
    const Parameters p = clampParameters(rawParams);
    const float sr = sampleRate > 0.0f ? sampleRate : 44100.0f;

    if (!state.primed || p.crossLowHz != state.lastLow || p.crossHighHz != state.lastHigh) {
        state.lowLp = butterworth(p.crossLowHz, sr, false);
        state.lowHp = butterworth(p.crossLowHz, sr, true);
        state.highLp = butterworth(p.crossHighHz, sr, false);
        state.highHp = butterworth(p.crossHighHz, sr, true);
        state.lastLow = p.crossLowHz;
        state.lastHigh = p.crossHighHz;
    }

    const float inGoal = std::exp(p.inputDb * kDbToNeper);
    const float outGoal = std::exp(p.outputDb * kDbToNeper);
    const float amountGoal = p.amount * 0.01f;
    if (!state.primed) {
        state.inputLinear = inGoal;
        state.outputLinear = outGoal;
        state.amount = amountGoal;
        state.primed = true;
    }
    const float inFrom = state.inputLinear, outFrom = state.outputLinear, amountFrom = state.amount;
    const float invFrames = frames > 0 ? 1.0f / static_cast<float>(frames) : 0.0f;

    std::array<float, kBands> attack {}, release {};
    for (int b = 0; b < kBands; ++b) {
        const Band& band = p.bands[static_cast<size_t>(b)];
        attack[static_cast<size_t>(b)] = coefficient(band.attackMs * p.time, sr);
        release[static_cast<size_t>(b)] = coefficient(band.releaseMs * p.time, sr);
    }
    // The detector holds each band's peak and lets it decay over about two periods of the band's
    // lowest frequency, so a sine reads as its true peak instead of a rectified wave.
    std::array<float, kBands> detectorDecay {};
    {
        const float edges[kBands] = {p.crossLowHz * 0.25f, p.crossLowHz, p.crossHighHz};
        for (int b = 0; b < kBands; ++b)
            detectorDecay[static_cast<size_t>(b)] = coefficient(std::min(std::max(2000.0f / edges[b], 0.3f), 60.0f), sr);
    }
    std::array<float, kBands> bandLinear {};
    for (int b = 0; b < kBands; ++b) bandLinear[static_cast<size_t>(b)] = p.bands[static_cast<size_t>(b)].gainDb;

    const float* in0 = inputs[0];
    const float* in1 = inputs[1] ? inputs[1] : inputs[0];
    float* out0 = outputs[0];
    float* out1 = outputs[1];
    std::array<float, kBands> level {};

    for (uint32_t i = 0; i < frames; ++i) {
        float dry[2] = {in0[i], in1[i]};
        if (!std::isfinite(dry[0])) dry[0] = 0.0f;
        if (!std::isfinite(dry[1])) dry[1] = 0.0f;
        const float t = static_cast<float>(i + 1) * invFrames;
        const double drive = inFrom + (inGoal - inFrom) * t;

        // Split each channel into three bands; the low band goes through the all-pass of the
        // high crossover so the three sum back to a flat response.
        double band[2][kBands];
        for (int c = 0; c < 2; ++c) {
            ChannelState& cs = state.channels[static_cast<size_t>(c)];
            const double x = static_cast<double>(dry[c]) * drive;
            const double lowRaw = runLr4(cs.lowLp, state.lowLp, x);
            const double rest = runLr4(cs.lowHp, state.lowHp, x);
            band[c][0] = runLr4(cs.lowApLp, state.highLp, lowRaw) + runLr4(cs.lowApHp, state.highHp, lowRaw);
            band[c][1] = runLr4(cs.midLp, state.highLp, rest);
            band[c][2] = runLr4(cs.highHp, state.highHp, rest);
        }

        double sum[2] = {0.0, 0.0};
        for (int b = 0; b < kBands; ++b) {
            const size_t bi = static_cast<size_t>(b);
            const Band& bd = p.bands[bi];
            if (bd.on < 0.5f) {
                state.envelopeDb[bi] = 0.0f;
                state.detector[bi] = 0.0f;
                sum[0] += band[0][b];
                sum[1] += band[1][b];
                level[bi] = kFloorLevel;
                continue;
            }
            // One detector per band, shared by both channels, so the stereo image holds still.
            const float instant = static_cast<float>(std::max(std::fabs(band[0][b]), std::fabs(band[1][b])));
            state.detector[bi] = std::max(instant, state.detector[bi] * detectorDecay[bi]);
            const float peak = std::max(state.detector[bi], kFloorLevel);
            level[bi] = peak;
            const float target = bandTargetDb(bd, 20.0f * std::log10(peak), p.upLimitDb);
            const float coeff = target < state.envelopeDb[bi] ? attack[bi] : release[bi];
            state.envelopeDb[bi] = coeff * state.envelopeDb[bi] + (1.0f - coeff) * target;
            const double gain = std::exp((state.envelopeDb[bi] + bandLinear[bi]) * kDbToNeper);
            sum[0] += band[0][b] * gain;
            sum[1] += band[1][b] * gain;
        }

        const float outGain = outFrom + (outGoal - outFrom) * t;
        const float amount = amountFrom + (amountGoal - amountFrom) * t;
        const float wet0 = static_cast<float>(sum[0] * outGain), wet1 = static_cast<float>(sum[1] * outGain);
        out0[i] = dry[0] + (wet0 - dry[0]) * amount;
        out1[i] = dry[1] + (wet1 - dry[1]) * amount;
    }

    // Recursive memory on a silent input decays into denormals; zero what is negligible.
    constexpr double kTiny = 1.0e-30;
    for (ChannelState& cs : state.channels)
        for (Lr4* f : {&cs.lowLp, &cs.lowHp, &cs.midLp, &cs.highHp, &cs.lowApLp, &cs.lowApHp})
            for (Cell& c : f->cell) {
                if (std::fabs(c.z1) < kTiny) c.z1 = 0.0;
                if (std::fabs(c.z2) < kTiny) c.z2 = 0.0;
            }
    for (float& e : state.envelopeDb)
        if (std::fabs(e) < 1.0e-6f) e = 0.0f;
    for (float& d : state.detector)
        if (d < 1.0e-20f) d = 0.0f;

    state.inputLinear = inGoal;
    state.outputLinear = outGoal;
    state.amount = amountGoal;
    for (int b = 0; b < kBands; ++b) {
        state.meters.gainDb[static_cast<size_t>(b)] = state.envelopeDb[static_cast<size_t>(b)];
        state.meters.levelDb[static_cast<size_t>(b)] = 20.0f * std::log10(std::max(level[static_cast<size_t>(b)], kFloorLevel));
    }
}

}  // namespace downspout::trio
