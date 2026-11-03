#include "silken_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::silken {

namespace {

constexpr double kPi = 3.14159265358979323846;

float clampf(const float v, const float lo, const float hi, const float fallback) noexcept
{
    if (!std::isfinite(v)) return fallback;
    return std::min(hi, std::max(lo, v));
}

double dbToGain(const double db) noexcept { return std::pow(10.0, db / 20.0); }

void normalise(Biquad& f, double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b2 / a0; f.a1 = a1 / a0; f.a2 = a2 / a0;
}

// RBJ cookbook designs. The state (z1, z2) is left alone so a coefficient change does not click.
struct Angle { double cs, sn; };
Angle angle(const double hz, const double sr) noexcept
{
    const double w = 2.0 * kPi * std::min(hz, 0.45 * sr) / sr;
    return {std::cos(w), std::sin(w)};
}

void designHighPass(Biquad& f, const double hz, const double sr) noexcept
{
    const auto a = angle(hz, sr);
    const double alpha = a.sn / (2.0 * 0.70710678118654752);
    normalise(f, (1 + a.cs) / 2, -(1 + a.cs), (1 + a.cs) / 2, 1 + alpha, -2 * a.cs, 1 - alpha);
}

void designLowPass(Biquad& f, const double hz, const double sr) noexcept
{
    const auto a = angle(hz, sr);
    const double alpha = a.sn / (2.0 * 0.70710678118654752);
    normalise(f, (1 - a.cs) / 2, 1 - a.cs, (1 - a.cs) / 2, 1 + alpha, -2 * a.cs, 1 - alpha);
}

void designBandPass(Biquad& f, const double hz, const double q, const double sr) noexcept
{
    const auto a = angle(hz, sr);
    const double alpha = a.sn / (2.0 * q);
    normalise(f, alpha, 0.0, -alpha, 1 + alpha, -2 * a.cs, 1 - alpha);   // 0 dB at the centre
}

void designPeak(Biquad& f, const double hz, const double q, const double db, const double sr) noexcept
{
    const auto a = angle(hz, sr);
    const double A = std::pow(10.0, db / 40.0), alpha = a.sn / (2.0 * q);
    normalise(f, 1 + alpha * A, -2 * a.cs, 1 - alpha * A, 1 + alpha / A, -2 * a.cs, 1 - alpha / A);
}

void designLowShelf(Biquad& f, const double hz, const double db, const double sr) noexcept
{
    const auto a = angle(hz, sr);
    const double A = std::pow(10.0, db / 40.0), beta = 2.0 * std::sqrt(A) * (a.sn / (2.0 * 0.70710678118654752));
    normalise(f, A * ((A + 1) - (A - 1) * a.cs + beta), 2 * A * ((A - 1) - (A + 1) * a.cs), A * ((A + 1) - (A - 1) * a.cs - beta),
              (A + 1) + (A - 1) * a.cs + beta, -2 * ((A - 1) + (A + 1) * a.cs), (A + 1) + (A - 1) * a.cs - beta);
}

void designHighShelf(Biquad& f, const double hz, const double db, const double sr) noexcept
{
    const auto a = angle(hz, sr);
    const double A = std::pow(10.0, db / 40.0), beta = 2.0 * std::sqrt(A) * (a.sn / (2.0 * 0.70710678118654752));
    normalise(f, A * ((A + 1) + (A - 1) * a.cs + beta), -2 * A * ((A - 1) + (A + 1) * a.cs), A * ((A + 1) + (A - 1) * a.cs - beta),
              (A + 1) - (A - 1) * a.cs + beta, 2 * ((A - 1) - (A + 1) * a.cs), (A + 1) - (A - 1) * a.cs - beta);
}

double run(Biquad& f, const double x) noexcept
{
    const double y = f.b0 * x + f.z1;
    f.z1 = f.b1 * x - f.a1 * y + f.z2;
    f.z2 = f.b2 * x - f.a2 * y;
    // Idle filters must not decay into denormals. Both states go together: zeroing one alone breaks the decay of a section whose a1 is above 1.
    if (std::abs(f.z1) < 1e-20 && std::abs(f.z2) < 1e-20) { f.z1 = 0.0; f.z2 = 0.0; }
    return y;
}

double follow(double& env, const double x, const double attack, const double release) noexcept
{
    env += (x - env) * (x > env ? attack : release);
    if (env < 1e-12) env = 0.0;
    return env;
}

double coefficient(const double seconds, const double sr) noexcept { return 1.0 - std::exp(-1.0 / (seconds * sr)); }

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters c = p;
    const auto sw = [](const float v, const float fallback) { return std::isfinite(v) && v >= 0.5f ? 1.0f : (std::isfinite(v) ? 0.0f : fallback); };
    c.hpfOn = sw(p.hpfOn, 1.0f);
    c.hpfHz = clampf(p.hpfHz, kHpfMin, kHpfMax, 80.0f);
    c.plosiveOn = sw(p.plosiveOn, 1.0f);
    c.plosiveThrDb = clampf(p.plosiveThrDb, kPlosiveThrMin, kPlosiveThrMax, -20.0f);
    c.essOn = sw(p.essOn, 1.0f);
    c.essFreqHz = clampf(p.essFreqHz, kEssFreqMin, kEssFreqMax, 6500.0f);
    c.essThrDb = clampf(p.essThrDb, kEssThrMin, kEssThrMax, -30.0f);
    c.essRangeDb = clampf(p.essRangeDb, 0.0f, kEssRangeMax, 10.0f);
    c.essMode = sw(p.essMode, 0.0f);
    c.essListen = sw(p.essListen, 0.0f);
    c.warmthDb = clampf(p.warmthDb, -kToneMaxDb, kToneMaxDb, 0.0f);
    c.presenceDb = clampf(p.presenceDb, -kToneMaxDb, kToneMaxDb, 0.0f);
    c.airDb = clampf(p.airDb, -kToneMaxDb, kToneMaxDb, 0.0f);
    c.sparkle = clampf(p.sparkle, 0.0f, 1.0f, 0.0f);
    c.outputDb = clampf(p.outputDb, kOutputMinDb, kOutputMaxDb, 0.0f);
    return c;
}

double reductionDb(const double levelDb, const double thresholdDb, const double ratio) noexcept
{
    const double over = levelDb - thresholdDb;
    double effective;
    if (over <= -kKneeDb / 2.0) effective = 0.0;
    else if (over >= kKneeDb / 2.0) effective = over;
    else effective = (over + kKneeDb / 2.0) * (over + kKneeDb / 2.0) / (2.0 * kKneeDb);
    return effective * (1.0 - 1.0 / ratio);
}

void resetState(EngineState& state) noexcept
{
    state = EngineState {};
}

void processBlock(EngineState& s, const Parameters& params, const float sampleRate, const std::uint32_t frames, const float* inLeft, const float* inRight,
                  float* outLeft, float* outRight) noexcept
{
    const Parameters p = clampParameters(params);
    const double sr = std::max(8000.0, static_cast<double>(sampleRate));
    const double smooth = coefficient(kSmoothSeconds, sr);
    const double gainFast = coefficient(kGainSmoothSeconds, sr);
    const double plAttack = coefficient(kPlosiveAttackS, sr), plRelease = coefficient(kPlosiveReleaseS, sr);
    const double esAttack = coefficient(kEssAttackS, sr), esRelease = coefficient(kEssReleaseS, sr);
    const bool wide = p.essMode >= 0.5f, listen = p.essListen >= 0.5f;
    const double targetOut = dbToGain(p.outputDb);

    if (!s.started) {
        s.started = true;
        s.hpfHz = p.hpfHz; s.essFreqHz = p.essFreqHz;
        s.warmthDb = p.warmthDb; s.presenceDb = p.presenceDb; s.airDb = p.airDb;
        s.sparkle = p.sparkle; s.outputGain = targetOut;
    }
    const auto glide = [smooth](double& v, const double target) {
        v += (target - v) * smooth;
        if (std::abs(target - v) < 1e-6) v = target;
    };

    for (std::uint32_t n = 0; n < frames; ++n) {
        glide(s.hpfHz, p.hpfHz); glide(s.essFreqHz, p.essFreqHz);
        glide(s.warmthDb, p.warmthDb); glide(s.presenceDb, p.presenceDb); glide(s.airDb, p.airDb);
        glide(s.sparkle, p.sparkle); glide(s.outputGain, targetOut);

        // Filter coefficients follow the smoothed values, remade every 16 samples and only when something moved.
        if ((s.counter++ & 15) == 0) {
            if (s.hpfHz != s.madeHpf) {
                s.madeHpf = s.hpfHz;
                for (auto& f : s.hpf) designHighPass(f, s.hpfHz, sr);
            }
            if (s.essFreqHz != s.madeEss) {
                s.madeEss = s.essFreqHz;
                for (auto& f : s.essBand) designBandPass(f, s.essFreqHz, kEssQ, sr);
                for (auto& f : s.essBand2) designBandPass(f, s.essFreqHz, kEssQ, sr);
            }
            if (s.warmthDb != s.madeWarmth) { s.madeWarmth = s.warmthDb; for (auto& f : s.warmth) designLowShelf(f, kWarmthHz, s.warmthDb, sr); }
            if (s.presenceDb != s.madePresence) { s.madePresence = s.presenceDb; for (auto& f : s.presence) designPeak(f, kPresenceHz, kPresenceQ, s.presenceDb, sr); }
            if (s.airDb != s.madeAir) { s.madeAir = s.airDb; for (auto& f : s.air) designHighShelf(f, kAirHz, s.airDb, sr); }
            if (!s.fixedMade) {   // the fixed filters, once
                s.fixedMade = true;
                for (auto& f : s.plosiveLow) designLowPass(f, kPlosiveHz, sr);
                for (auto& f : s.plosiveHigh) designHighPass(f, kPlosiveHz, sr);
                for (auto& f : s.sparkleLow) designLowPass(f, kSparkleFromHz, sr);
                for (auto& f : s.sparkleHarm) designLowPass(f, kSparkleHarmonicsHz, sr);
            }
        }

        double x[2] = {inLeft[n], inRight[n]};
        double y[2] = {x[0], x[1]};

        // Rumble filter.
        if (p.hpfOn >= 0.5f) for (int c = 0; c < 2; ++c) y[c] = run(s.hpf[static_cast<std::size_t>(c)], y[c]);

        // Plosive guard: gain on the low band only.
        {
            double high[2], peak = 0.0;
            for (int c = 0; c < 2; ++c) {
                const auto i = static_cast<std::size_t>(c);
                peak = std::max(peak, std::abs(run(s.plosiveLow[i], y[c])));
                high[c] = run(s.plosiveHigh[i], y[c]);
            }
            const double env = follow(s.plosiveEnv, peak, plAttack, plRelease);
            const double db = 20.0 * std::log10(std::max(env, 1e-9));
            const double gr = p.plosiveOn >= 0.5f ? std::min(static_cast<double>(kEssRangeMax), reductionDb(db, p.plosiveThrDb, kPlosiveRatio)) : 0.0;
            const double target = dbToGain(-gr);
            s.plosiveGain += (target - s.plosiveGain) * (target < s.plosiveGain ? 1.0 : gainFast);   // down at once (the envelope already attacks), up in 1 ms
            if (std::abs(1.0 - s.plosiveGain) < 1e-7) s.plosiveGain = 1.0;
            for (int c = 0; c < 2; ++c) y[c] = y[c] * s.plosiveGain + high[c] * (1.0 - s.plosiveGain);
            s.meters.plosiveReductionDb = static_cast<float>(-20.0 * std::log10(s.plosiveGain));
        }

        // De-esser.
        {
            double band[2], detector[2], peak = 0.0;
            for (int c = 0; c < 2; ++c) {
                const auto i = static_cast<std::size_t>(c);
                band[c] = run(s.essBand[i], y[c]);
                detector[c] = run(s.essBand2[i], band[c]);
                peak = std::max(peak, std::abs(detector[c]));
            }
            const double env = follow(s.essEnv, peak, esAttack, esRelease);
            const double db = 20.0 * std::log10(std::max(env, 1e-9));
            const double gr = p.essOn >= 0.5f ? std::min(static_cast<double>(p.essRangeDb), reductionDb(db, p.essThrDb, kEssRatio)) : 0.0;
            const double target = dbToGain(-gr);
            s.essGain += (target - s.essGain) * (target < s.essGain ? 1.0 : gainFast);
            if (std::abs(1.0 - s.essGain) < 1e-7) s.essGain = 1.0;
            for (int c = 0; c < 2; ++c) {
                if (listen) y[c] = detector[c];
                else if (wide) y[c] *= s.essGain;
                else y[c] -= (1.0 - s.essGain) * band[c];
            }
            s.meters.essReductionDb = static_cast<float>(-20.0 * std::log10(s.essGain));
            s.meters.detectorDb = static_cast<float>(std::max(-60.0, db));
        }

        // Voice EQ and sparkle (not in Listen).
        if (!listen) {
            for (int c = 0; c < 2; ++c) {
                const auto i = static_cast<std::size_t>(c);
                if (s.warmthDb != 0.0) y[c] = run(s.warmth[i], y[c]);
                if (s.presenceDb != 0.0) y[c] = run(s.presence[i], y[c]);
                if (s.airDb != 0.0) y[c] = run(s.air[i], y[c]);
                if (s.sparkle > 0.0) {
                    const double high = y[c] - run(s.sparkleLow[i], y[c]);
                    const double harmonics = std::tanh(kSparkleDrive * high) / kSparkleDrive - high;
                    y[c] += s.sparkle * kSparkleGain * (harmonics - run(s.sparkleHarm[i], harmonics));
                }
            }
        }

        outLeft[n] = static_cast<float>(y[0] * s.outputGain);
        outRight[n] = static_cast<float>(y[1] * s.outputGain);
    }
}

}  // namespace downspout::silken
