#include "gate_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::gate {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTiny = 1.0e-20;
constexpr double kDetectorAttackMs = 2.0;
constexpr double kDetectorReleaseMs = 40.0;
constexpr double kControlSmoothMs = 5.0;

[[nodiscard]] double alphaFor(const double ms, const double sr) noexcept
{
    if (ms <= 0.0 || !(sr > 0.0))
        return 1.0;
    return 1.0 - std::exp(-1.0 / (0.001 * ms * sr));
}

[[nodiscard]] double finite(const double x) noexcept { return std::isfinite(x) ? x : 0.0; }

[[nodiscard]] int framesFor(const double ms, const double sr) noexcept
{
    return static_cast<int>(std::lround(0.001 * ms * sr));
}

}  // namespace

void activate(Engine& e, const double sampleRate)
{
    e.sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    e.detectorAttack = alphaFor(kDetectorAttackMs, e.sampleRate);
    e.detectorRelease = alphaFor(kDetectorReleaseMs, e.sampleRate);
    e.controlAlpha = alphaFor(kControlSmoothMs, e.sampleRate);
    reset(e);
}

void reset(Engine& e)
{
    e.primed = false;
    e.hpIn[0] = e.hpIn[1] = 0.0;
    e.hpOut[0] = e.hpOut[1] = 0.0;
    e.detector = 0.0;
    e.open = false;
    e.holdRemaining = 0;
    e.envelope = 0.0;
    e.bypassMix = 0.0;
    e.outputGain = 1.0;
    e.lastLevelDb = -100.0;
    e.lastReductionDb = 0.0;
    e.lastOpen = false;
}

void processBlock(Engine& e, const Parameters& raw, const std::uint32_t frames, const float* const* inputs,
                  float* const* outputs) noexcept
{
    const Parameters p = clampParameters(raw);
    const double sr = e.sampleRate;
    e.gateAttack = alphaFor(p.attack, sr);
    e.gateRelease = alphaFor(p.release, sr);

    const double hpCoeff = p.scHpf > kMinScHpfHz + 0.5 ? 1.0 / (1.0 + 2.0 * kPi * p.scHpf / sr) : 0.0;
    const double threshold = static_cast<double>(p.threshold);
    const double closeAt = threshold - static_cast<double>(p.hysteresis);
    const double range = static_cast<double>(p.range);
    const int holdFrames = framesFor(p.hold, sr);
    const double targetGain = std::pow(10.0, static_cast<double>(p.outputDb) / 20.0);
    const double targetBypass = p.bypass >= 0.5 ? 1.0 : 0.0;

    if (!e.primed) {
        e.outputGain = targetGain;
        e.bypassMix = targetBypass;
        e.primed = true;
    }

    for (std::uint32_t s = 0; s < frames; ++s) {
        const double mainL = finite(inputs[0][s]);
        const double mainR = finite(inputs[1][s]);
        const int source = externalSidechain(p) ? 2 : 0;
        double scL = finite(inputs[source][s]);
        double scR = finite(inputs[source + 1][s]);

        for (int c = 0; c < 2; ++c) {
            double& in = c == 0 ? scL : scR;
            double& hpIn = e.hpIn[c];
            double& hpOut = e.hpOut[c];
            if (hpCoeff > 0.0) {
                const double x = in;
                hpOut = hpCoeff * (hpOut + x - hpIn);
                hpIn = x;
                in = hpOut;
            } else {
                hpIn = in;
                hpOut = 0.0;
            }
            if (std::fabs(hpIn) < kTiny)
                hpIn = 0.0;
            if (std::fabs(hpOut) < kTiny)
                hpOut = 0.0;
        }

        const double meanSquare = 0.5 * (scL * scL + scR * scR);
        const double detectorAlpha = meanSquare > e.detector ? e.detectorAttack : e.detectorRelease;
        e.detector += (meanSquare - e.detector) * detectorAlpha;
        if (e.detector < kTiny)
            e.detector = 0.0;
        e.lastLevelDb = 10.0 * std::log10(std::max(e.detector, kTiny));

        if (!e.open) {
            if (e.lastLevelDb > threshold) {
                e.open = true;
                e.holdRemaining = holdFrames;
            }
        } else if (e.lastLevelDb < closeAt) {
            if (e.holdRemaining > 0)
                --e.holdRemaining;
            else
                e.open = false;
        } else {
            e.holdRemaining = holdFrames;
        }
        e.lastOpen = e.open;

        const double target = e.open ? 1.0 : 0.0;
        const double envelopeAlpha = e.open ? e.gateAttack : e.gateRelease;
        e.envelope += (target - e.envelope) * envelopeAlpha;
        if (!e.open && e.envelope < kTiny)
            e.envelope = 0.0;

        e.lastReductionDb = -range * (1.0 - e.envelope);
        const double gain = std::pow(10.0, e.lastReductionDb / 20.0);
        e.outputGain += (targetGain - e.outputGain) * e.controlAlpha;
        e.bypassMix += (targetBypass - e.bypassMix) * e.controlAlpha;

        const double wetGain = gain * e.outputGain;
        outputs[0][s] = static_cast<float>(mainL * ((1.0 - e.bypassMix) * wetGain + e.bypassMix));
        outputs[1][s] = static_cast<float>(mainR * ((1.0 - e.bypassMix) * wetGain + e.bypassMix));
    }
}

double detectorDb(const Engine& e) noexcept { return e.lastLevelDb; }
double reductionDb(const Engine& e) noexcept { return e.lastReductionDb; }
bool gateIsOpen(const Engine& e) noexcept { return e.lastOpen; }
double gateEnvelope(const Engine& e) noexcept { return e.envelope; }

}  // namespace downspout::gate
