#include "whirligig_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::whirligig {

namespace {

constexpr double kTwoPi = 6.283185307179586;

float clampf(const float v, const float lo, const float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Four-point Hermite between y0 and y1 for t in [0,1].
float hermite(const float ym1, const float y0, const float y1, const float y2, const float t) noexcept
{
    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

// `ring[pos]` is the newest sample; `back` (>= 1) samples behind it.
float readBack(const std::array<float, kDelayRing>& ring, const int pos, const float back) noexcept
{
    const int n = static_cast<int>(back);
    const float f = back - static_cast<float>(n);
    constexpr int mask = kDelayRing - 1;
    const auto at = [&](const int k) { return ring[static_cast<std::size_t>((pos - k) & mask)]; };
    return hermite(at(n - 1), at(n), at(n + 1), at(n + 2), f);
}

void designButterworth(Biquad& q, const double cutoff, const double sampleRate, const bool high) noexcept
{
    const double w0 = kTwoPi * std::min(cutoff, 0.45 * sampleRate) / sampleRate;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s / (2.0 * 0.7071067811865476);
    const double a0 = 1.0 + alpha;
    const double b0 = high ? (1.0 + c) * 0.5 : (1.0 - c) * 0.5;
    const double b1 = high ? -(1.0 + c) : (1.0 - c);
    q.b0 = static_cast<float>(b0 / a0);
    q.b1 = static_cast<float>(b1 / a0);
    q.b2 = q.b0;
    q.a1 = static_cast<float>(-2.0 * c / a0);
    q.a2 = static_cast<float>((1.0 - alpha) / a0);
}

inline float run(Biquad& q, const float x) noexcept
{
    const float y = q.b0 * x + q.z[0];
    q.z[0] = q.b1 * x - q.a1 * y + q.z[1];
    q.z[1] = q.b2 * x - q.a2 * y;
    if (std::fabs(q.z[0]) < 1.0e-20f) q.z[0] = 0.0f;   // no denormals while idle
    if (std::fabs(q.z[1]) < 1.0e-20f) q.z[1] = 0.0f;
    return y;
}

double wrapTurn(double t) noexcept
{
    t -= std::floor(t);
    return t;
}

}  // namespace

Parameters clampParameters(const Parameters& in) noexcept
{
    Parameters p = in;
    p.speed = std::round(clampf(p.speed, 0.0f, 2.0f));
    p.hornSlowHz = clampf(p.hornSlowHz, 0.3f, 1.5f);
    p.hornFastHz = clampf(p.hornFastHz, 4.0f, 9.0f);
    p.drumSlowHz = clampf(p.drumSlowHz, 0.3f, 1.2f);
    p.drumFastHz = clampf(p.drumFastHz, 3.0f, 8.0f);
    p.hornAccelS = clampf(p.hornAccelS, 0.1f, 4.0f);
    p.drumAccelS = clampf(p.drumAccelS, 0.5f, 10.0f);
    p.crossoverHz = clampf(p.crossoverHz, 300.0f, 2000.0f);
    p.hornDepth = clampf(p.hornDepth, 0.0f, 1.0f);
    p.hornDoppler = clampf(p.hornDoppler, 0.0f, 1.0f);
    p.drumDepth = clampf(p.drumDepth, 0.0f, 1.0f);
    p.drumDoppler = clampf(p.drumDoppler, 0.0f, 1.0f);
    p.micAngle = clampf(p.micAngle, 0.0f, 180.0f);
    p.hornLevelDb = clampf(p.hornLevelDb, -24.0f, 12.0f);
    p.drumLevelDb = clampf(p.drumLevelDb, -24.0f, 12.0f);
    p.mix = clampf(p.mix, 0.0f, 1.0f);
    p.outputDb = clampf(p.outputDb, -24.0f, 12.0f);
    p.ccChannel = std::round(clampf(p.ccChannel, 0.0f, 16.0f));
    p.sync = std::round(clampf(p.sync, 0.0f, 1.0f));
    const float top = static_cast<float>(kDivisionCount - 1);
    p.hornSlowDiv = std::round(clampf(p.hornSlowDiv, 0.0f, top));
    p.hornFastDiv = std::round(clampf(p.hornFastDiv, 0.0f, top));
    p.drumSlowDiv = std::round(clampf(p.drumSlowDiv, 0.0f, top));
    p.drumFastDiv = std::round(clampf(p.drumFastDiv, 0.0f, top));
    p.syncPhase = clampf(p.syncPhase, 0.0f, 360.0f);
    return p;
}

double beatsPerTurn(const float index) noexcept
{
    return kBeatsPerTurn[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(index)), 0, kDivisionCount - 1))];
}

double lockedTurn(const double beatPosition, const double beats, const double phaseDegrees) noexcept
{
    return wrapTurn(beatPosition / beats + phaseDegrees / 360.0);
}

double targetHz(const Parameters& p, const bool horn, const double bpm) noexcept
{
    const auto speed = static_cast<Speed>(static_cast<int>(std::lround(p.speed)));
    if (speed == Speed::Stop) return 0.0;
    const bool fast = speed == Speed::Fast;
    if (p.sync >= 0.5f) {
        const double beatsPerSecond = (bpm > 0.0 ? bpm : kFallbackBpm) / 60.0;
        const float index = horn ? (fast ? p.hornFastDiv : p.hornSlowDiv) : (fast ? p.drumFastDiv : p.drumSlowDiv);
        return beatsPerSecond / beatsPerTurn(index);
    }
    return horn ? (fast ? p.hornFastHz : p.hornSlowHz) : (fast ? p.drumFastHz : p.drumSlowHz);
}

double lobe(const double rotorTurn, const double micTurn) noexcept
{
    return 0.5 * (1.0 + std::cos(kTwoPi * (rotorTurn - micTurn)));
}

void releaseOverride(EngineState& s) noexcept
{
    s.overrideActive = false;
}

int dryDelaySamples(const float sampleRate) noexcept
{
    return static_cast<int>(std::lround(kBaseDelayMs * 0.001 * static_cast<double>(sampleRate)));
}

void resetState(EngineState& s) noexcept
{
    s.hornTurn = s.drumTurn = 0.0;
    s.hornHz = s.drumHz = 0.0;
    for (auto& q : s.lowPass) q.z = {};
    for (auto& q : s.highPass) q.z = {};
    s.hornRing.fill(0.0f);
    s.drumRing.fill(0.0f);
    for (auto& r : s.dryRing) r.fill(0.0f);
    s.pos = s.dryPos = 0;
    s.overrideActive = false;
    s.meters = {};
    s.started = true;
}

void processBlock(EngineState& s, const Parameters& rawParams, const float sampleRate, const std::uint32_t frames,
                  const Transport& transport, const MidiEvent* events, const std::uint32_t eventCount, const float* inLeft, const float* inRight,
                  float* outLeft, float* outRight) noexcept
{
    if (!s.started) resetState(s);
    if (sampleRate <= 0.0f) {
        std::fill(outLeft, outLeft + frames, 0.0f);
        std::fill(outRight, outRight + frames, 0.0f);
        return;
    }
    Parameters p = clampParameters(rawParams);
    const double sr = static_cast<double>(sampleRate);

    for (auto& q : s.lowPass) designButterworth(q, p.crossoverHz, sr, false);
    for (auto& q : s.highPass) designButterworth(q, p.crossoverHz, sr, true);

    // A whole number of samples, the same as the dry delay: with no Doppler the interpolator is exact (flat to the last
    // octave) and wet and dry line up. Doppler then swings the delay around it.
    const float baseDelay = static_cast<float>(dryDelaySamples(sampleRate));
    const float hornSwing = static_cast<float>(kHornRadius / kSpeedOfSound * sr) * p.hornDoppler;
    const float drumSwing = static_cast<float>(kDrumRadius / kSpeedOfSound * sr) * p.drumDoppler;
    const float hornLevel = std::pow(10.0f, p.hornLevelDb / 20.0f);
    const float drumLevel = std::pow(10.0f, p.drumLevelDb / 20.0f);
    const float outGain = std::pow(10.0f, p.outputDb / 20.0f);
    const double halfMic = static_cast<double>(p.micAngle) / 720.0;   // turns from the centre to each microphone
    const double hornCoef = 1.0 - std::exp(-1.0 / (static_cast<double>(p.hornAccelS) * sr));
    const double drumCoef = 1.0 - std::exp(-1.0 / (static_cast<double>(p.drumAccelS) * sr));
    const int dryDelay = dryDelaySamples(sampleRate);
    const int ccChannel = static_cast<int>(p.ccChannel) - 1;
    const bool timeKnown = transport.playing && transport.valid && transport.bpm > 0.0;
    const double bpm = transport.valid && transport.bpm > 0.0 ? transport.bpm : kFallbackBpm;
    const double beatsPerSample = bpm / (60.0 * sr);
    const double lockCoef = 1.0 - std::exp(-1.0 / (kLockSeconds * sr));
    const bool synced = p.sync >= 0.5f;
    bool locked = false;

    std::uint32_t next = 0;
    for (std::uint32_t i = 0; i < frames; ++i) {
        while (next < eventCount && events[next].frame <= i) {
            const MidiEvent& e = events[next++];
            if ((e.status & 0xF0) == 0xB0 && ccChannel >= 0 && (e.status & 0x0F) == ccChannel) {
                if (e.data1 == kCcSpeed) { s.overrideSpeed = e.data2 >= 64 ? 2.0f : 1.0f; s.overrideActive = true; }
                else if (e.data1 == kCcBrake) { s.overrideSpeed = e.data2 >= 64 ? 0.0f : 1.0f; s.overrideActive = true; }
            }
        }
        if (s.overrideActive) p.speed = s.overrideSpeed;
        const double hornTarget = targetHz(p, true, bpm), drumTarget = targetHz(p, false, bpm);

        s.hornHz += (hornTarget - s.hornHz) * hornCoef;
        s.drumHz += (drumTarget - s.drumHz) * drumCoef;
        if (hornTarget == 0.0 && s.hornHz < 1.0e-4) s.hornHz = 0.0;
        if (drumTarget == 0.0 && s.drumHz < 1.0e-4) s.drumHz = 0.0;
        s.hornTurn = wrapTurn(s.hornTurn + s.hornHz / sr);
        s.drumTurn = wrapTurn(s.drumTurn + s.drumHz / sr);

        // Beat mode: once a rotor is up to speed, glide its angle onto the one the transport position dictates. While it is
        // still accelerating (or the transport is stopped) it just turns at the tempo-derived rate.
        locked = false;
        if (synced && timeKnown && hornTarget > 0.0 && drumTarget > 0.0) {
            const double beat = transport.beatPosition + beatsPerSample * static_cast<double>(i);
            const auto lockOne = [&](double& turn, const double hz, const double target, const float division) {
                if (std::fabs(hz - target) > 0.01 * target) return false;
                const double want = lockedTurn(beat, beatsPerTurn(division), p.syncPhase);
                double err = wrapTurn(want - turn + 0.5) - 0.5;
                turn = wrapTurn(turn + err * lockCoef);
                err = wrapTurn(want - turn + 0.5) - 0.5;
                return std::fabs(err) < kLockedTolerance;
            };
            const bool fast = p.speed >= 1.5f;
            const bool h = lockOne(s.hornTurn, s.hornHz, hornTarget, fast ? p.hornFastDiv : p.hornSlowDiv);
            const bool d = lockOne(s.drumTurn, s.drumHz, drumTarget, fast ? p.drumFastDiv : p.drumSlowDiv);
            locked = h && d;
        }

        const float left = inLeft[i], right = inRight[i];
        const float mono = 0.5f * (left + right);
        float low = mono, high = mono;
        for (auto& q : s.lowPass) low = run(q, low);
        for (auto& q : s.highPass) high = run(q, high);

        s.pos = (s.pos + 1) & (kDelayRing - 1);
        s.hornRing[static_cast<std::size_t>(s.pos)] = high;
        s.drumRing[static_cast<std::size_t>(s.pos)] = low;

        for (int c = 0; c < 2; ++c) {
            const double mic = c == 0 ? halfMic : -halfMic;
            const double hornAngle = kTwoPi * (s.hornTurn - mic), drumAngle = kTwoPi * (s.drumTurn - mic);
            const float hornCos = static_cast<float>(std::cos(hornAngle)), drumCos = static_cast<float>(std::cos(drumAngle));
            const float hornLobe = 0.5f * (1.0f + hornCos), drumLobe = 0.5f * (1.0f + drumCos);
            // A source nearer the microphone is heard sooner: the delay falls as the rotor swings toward it.
            const float hornGain = 1.0f - p.hornDepth + p.hornDepth * hornLobe * hornLobe;   // the horn is tightly directional
            const float drumGain = 1.0f - p.drumDepth + p.drumDepth * drumLobe;
            const float hornSample = readBack(s.hornRing, s.pos, baseDelay - hornSwing * hornCos);
            const float drumSample = readBack(s.drumRing, s.pos, baseDelay - drumSwing * drumCos);
            const float wet = hornSample * hornGain * hornLevel + drumSample * drumGain * drumLevel;

            auto& dry = s.dryRing[static_cast<std::size_t>(c)];
            dry[static_cast<std::size_t>(s.dryPos)] = c == 0 ? left : right;
            const float dried = dry[static_cast<std::size_t>((s.dryPos - dryDelay) & (kDryRing - 1))];
            const float out = outGain * (dried * (1.0f - p.mix) + wet * p.mix);
            (c == 0 ? outLeft : outRight)[i] = out;
        }
        s.dryPos = (s.dryPos + 1) & (kDryRing - 1);
    }
    s.meters.hornHz = static_cast<float>(s.hornHz);
    s.meters.drumHz = static_cast<float>(s.drumHz);
    s.meters.hornTurn = static_cast<float>(s.hornTurn);
    s.meters.drumTurn = static_cast<float>(s.drumTurn);
    s.meters.locked = locked ? 1.0f : 0.0f;
}

}  // namespace downspout::whirligig
