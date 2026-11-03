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
    c.gateOn = sw(p.gateOn, 0.0f);
    c.gateThrDb = clampf(p.gateThrDb, kGateThrMin, kGateThrMax, -50.0f);
    c.gateRangeDb = clampf(p.gateRangeDb, 0.0f, kGateRangeMax, 12.0f);
    c.tameFreqHz = clampf(p.tameFreqHz, kTameFreqMin, kTameFreqMax, 3000.0f);
    c.tameThrDb = clampf(p.tameThrDb, kTameThrMin, kTameThrMax, -30.0f);
    c.tameAmountDb = clampf(p.tameAmountDb, 0.0f, kTameAmountMax, 0.0f);
    c.levelOn = sw(p.levelOn, 0.0f);
    c.levelThrDb = clampf(p.levelThrDb, kLevelThrMin, kLevelThrMax, -20.0f);
    c.levelRatio = clampf(p.levelRatio, kLevelRatioMin, kLevelRatioMax, 3.0f);
    c.levelMix = clampf(p.levelMix, 0.0f, 1.0f, 1.0f);
    c.mudDb = clampf(p.mudDb, -kToneMaxDb, kToneMaxDb, 0.0f);
    c.drive = clampf(p.drive, 0.0f, 1.0f, 0.0f);
    return c;
}

namespace {

// Soft knee: the part of the excess over the threshold that counts, `over` in dB.
double kneed(const double over) noexcept
{
    if (over <= -kKneeDb / 2.0) return 0.0;
    if (over >= kKneeDb / 2.0) return over;
    return (over + kKneeDb / 2.0) * (over + kKneeDb / 2.0) / (2.0 * kKneeDb);
}

}  // namespace

double reductionDb(const double levelDb, const double thresholdDb, const double ratio) noexcept
{
    return kneed(levelDb - thresholdDb) * (1.0 - 1.0 / ratio);
}

double expansionDb(const double levelDb, const double thresholdDb, const double ratio) noexcept
{
    return kneed(thresholdDb - levelDb) * (ratio - 1.0);
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
    const double gtAttack = coefficient(kGateAttackS, sr), gtRelease = coefficient(kGateReleaseS, sr);
    const double gtOpen = coefficient(kGateOpenS, sr), gtClose = coefficient(kGateCloseS, sr);
    const double lvAttack = coefficient(kLevelAttackS, sr), lvRelease = coefficient(kLevelReleaseS, sr);
    const double lvDetectAttack = coefficient(kLevelDetectAttackS, sr), lvDetectRelease = coefficient(kLevelDetectReleaseS, sr);
    const bool wide = p.essMode >= 0.5f, listen = p.essListen >= 0.5f;
    const double targetOut = dbToGain(p.outputDb);
    const double targetMakeup = p.levelOn >= 0.5f ? -static_cast<double>(p.levelThrDb) * (1.0 - 1.0 / p.levelRatio) * kLevelMakeup : 0.0;
    const double targetAmount = p.levelOn >= 0.5f ? 1.0 : 0.0;

    if (!s.started) {
        s.started = true;
        s.hpfHz = p.hpfHz; s.ess.freqHz = p.essFreqHz; s.tame.freqHz = p.tameFreqHz;
        s.warmthDb = p.warmthDb; s.presenceDb = p.presenceDb; s.airDb = p.airDb; s.mudDb = p.mudDb;
        s.sparkle = p.sparkle; s.drive = p.drive; s.outputGain = targetOut;
        s.tameRange = p.tameAmountDb; s.levelAmount = targetAmount; s.levelMakeupDb = targetMakeup; s.levelMix = p.levelMix;
    }
    const auto glide = [smooth](double& v, const double target) {
        v += (target - v) * smooth;
        if (std::abs(target - v) < 1e-6) v = target;
    };

    // One detector-and-cut stage, shared by the de-esser and the harshness tamer. Returns the detector level (dB) and sets `reduction` (dB).
    const auto dynBand = [&](DynBand& d, double y[2], const bool active, const double thrDb, const double rangeDb, const double ratio,
                             const bool wideMode, const bool listenMode, double& levelDb) {
        double band[2], detector[2], peak = 0.0;
        for (int c = 0; c < 2; ++c) {
            const auto i = static_cast<std::size_t>(c);
            band[c] = run(d.cut[i], y[c]);
            detector[c] = run(d.detect[i], band[c]);
            peak = std::max(peak, std::abs(detector[c]));
        }
        const double env = follow(d.env, peak, esAttack, esRelease);
        levelDb = 20.0 * std::log10(std::max(env, 1e-9));
        const double gr = active ? std::min(rangeDb, reductionDb(levelDb, thrDb, ratio)) : 0.0;
        const double target = dbToGain(-gr);
        d.gain += (target - d.gain) * (target < d.gain ? 1.0 : gainFast);   // down at once (the envelope already attacks), up in 1 ms
        if (std::abs(1.0 - d.gain) < 1e-7) d.gain = 1.0;
        for (int c = 0; c < 2; ++c) {
            if (listenMode) y[c] = detector[c];
            else if (wideMode) y[c] *= d.gain;
            else y[c] -= (1.0 - d.gain) * band[c];
        }
    };

    for (std::uint32_t n = 0; n < frames; ++n) {
        glide(s.hpfHz, p.hpfHz); glide(s.ess.freqHz, p.essFreqHz); glide(s.tame.freqHz, p.tameFreqHz);
        glide(s.warmthDb, p.warmthDb); glide(s.presenceDb, p.presenceDb); glide(s.airDb, p.airDb); glide(s.mudDb, p.mudDb);
        glide(s.sparkle, p.sparkle); glide(s.drive, p.drive); glide(s.outputGain, targetOut);
        glide(s.tameRange, p.tameAmountDb); glide(s.levelAmount, targetAmount); glide(s.levelMakeupDb, targetMakeup); glide(s.levelMix, p.levelMix);

        // Filter coefficients follow the smoothed values, remade every 16 samples and only when something moved.
        if ((s.counter++ & 15) == 0) {
            if (s.hpfHz != s.madeHpf) {
                s.madeHpf = s.hpfHz;
                for (auto& f : s.hpf) designHighPass(f, s.hpfHz, sr);
            }
            for (DynBand* d : {&s.ess, &s.tame}) {
                if (d->freqHz != d->made) {
                    d->made = d->freqHz;
                    for (auto& f : d->cut) designBandPass(f, d->freqHz, kEssQ, sr);
                    for (auto& f : d->detect) designBandPass(f, d->freqHz, kEssQ, sr);
                }
            }
            if (s.mudDb != s.madeMud) { s.madeMud = s.mudDb; for (auto& f : s.mud) designPeak(f, kMudHz, kMudQ, s.mudDb, sr); }
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

        double y[2] = {inLeft[n], inRight[n]};

        // Rumble filter.
        if (p.hpfOn >= 0.5f) for (int c = 0; c < 2; ++c) y[c] = run(s.hpf[static_cast<std::size_t>(c)], y[c]);

        // Plosive guard: a detector on the low band, then a cross-fade towards a high-pass.
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
            s.plosiveGain += (target - s.plosiveGain) * (target < s.plosiveGain ? 1.0 : gainFast);
            if (std::abs(1.0 - s.plosiveGain) < 1e-7) s.plosiveGain = 1.0;
            for (int c = 0; c < 2; ++c) y[c] = y[c] * s.plosiveGain + high[c] * (1.0 - s.plosiveGain);
            s.meters.plosiveReductionDb = static_cast<float>(-20.0 * std::log10(s.plosiveGain));
        }

        // Gate: a 2:1 expander on the louder channel. The envelope releases over 80 ms, which is its hold; the gain closes over 40 ms and opens over 2 ms.
        {
            const double env = follow(s.gateEnv, std::max(std::abs(y[0]), std::abs(y[1])), gtAttack, gtRelease);
            const double db = 20.0 * std::log10(std::max(env, 1e-9));
            double gr = 0.0;
            if (p.gateOn >= 0.5f) gr = std::min(static_cast<double>(p.gateRangeDb), expansionDb(db, p.gateThrDb, kGateRatio));
            const double target = dbToGain(-gr);
            s.gateGain += (target - s.gateGain) * (target < s.gateGain ? gtClose : gtOpen);
            if (std::abs(target - s.gateGain) < 1e-7) s.gateGain = target;
            for (int c = 0; c < 2; ++c) y[c] *= s.gateGain;
            s.meters.gateReductionDb = static_cast<float>(-20.0 * std::log10(s.gateGain));
        }

        // Harshness tamer, then the de-esser.
        {
            double level = 0.0;
            dynBand(s.tame, y, s.tameRange > 0.0, p.tameThrDb, s.tameRange, kTameRatio, false, false, level);
        }
        {
            double level = 0.0;
            dynBand(s.ess, y, p.essOn >= 0.5f, p.essThrDb, p.essRangeDb, kEssRatio, wide, listen, level);
            s.meters.essReductionDb = static_cast<float>(-20.0 * std::log10(s.ess.gain));
            s.meters.detectorDb = static_cast<float>(std::max(-60.0, level));
        }

        // Voice EQ, drive, sparkle and the leveller (not in Listen).
        if (!listen) {
            // Leveller: a feed-forward compressor, blended in parallel.
            {
                const double env = follow(s.levelEnv, std::max(std::abs(y[0]), std::abs(y[1])), lvDetectAttack, lvDetectRelease);
                const double db = 20.0 * std::log10(std::max(env, 1e-9));
                const double target = targetAmount > 0.0 ? std::min(kLevelMaxDb, reductionDb(db, p.levelThrDb, p.levelRatio)) : 0.0;
                s.levelGr += (target - s.levelGr) * (target > s.levelGr ? lvAttack : lvRelease);
                if (std::abs(target - s.levelGr) < 1e-6) s.levelGr = target;
                if (s.levelAmount > 0.0) {
                    const double g = dbToGain(s.levelMakeupDb - s.levelGr);
                    const double k = 1.0 + s.levelAmount * s.levelMix * (g - 1.0);
                    y[0] *= k; y[1] *= k;
                }
                s.meters.levelReductionDb = static_cast<float>(s.levelGr * s.levelAmount);
            }
            for (int c = 0; c < 2; ++c) {
                const auto i = static_cast<std::size_t>(c);
                if (s.mudDb != 0.0) y[c] = run(s.mud[i], y[c]);
                if (s.warmthDb != 0.0) y[c] = run(s.warmth[i], y[c]);
                if (s.presenceDb != 0.0) y[c] = run(s.presence[i], y[c]);
                if (s.airDb != 0.0) y[c] = run(s.air[i], y[c]);
                if (s.drive > 0.0) {
                    const double a = 1.0 + 2.0 * s.drive;
                    y[c] = std::tanh(a * y[c]) / a;
                }
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
