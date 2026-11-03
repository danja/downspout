#include "wheelhouse_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::wheelhouse {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kTableSize = 4096;

float clampf(const float v, const float lo, const float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float roundClamp(const float v, const float lo, const float hi) noexcept
{
    return std::round(clampf(v, lo, hi));
}

struct SineTable {
    std::array<float, kTableSize + 1> v {};
    SineTable()
    {
        for (int i = 0; i <= kTableSize; ++i) v[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(2.0 * kPi * i / kTableSize));
    }
};

const SineTable& sineTable() noexcept
{
    static const SineTable t;
    return t;
}

inline float sineOf(const SineTable& t, const double phase) noexcept
{
    const double x = phase * kTableSize;
    const auto i = static_cast<int>(x);
    const float f = static_cast<float>(x - i);
    return t.v[static_cast<std::size_t>(i)] + f * (t.v[static_cast<std::size_t>(i) + 1] - t.v[static_cast<std::size_t>(i)]);
}

int channelOf(const float parameter) noexcept
{
    return static_cast<int>(parameter) - 1;   // -1 = off
}

void recomputeTargets(EngineState& s, const Parameters& p) noexcept
{
    for (auto& row : s.target) row.fill(0.0f);
    const std::array<const float*, 2> manualDrawbars = {p.upper.data(), p.lower.data()};
    for (int d = 0; d < kDivisionCount; ++d) {
        for (int key = kLowestKey; key <= kHighestKey; ++key) {
            if (!s.keyDown[static_cast<std::size_t>(d)][static_cast<std::size_t>(key)]) continue;
            if (d == kPedal) {
                for (int f = 0; f < kPedalDrawbarCount; ++f) {
                    const int w = wheelFor(key, kPedalSemitones[static_cast<std::size_t>(f)]);
                    if (w >= 0) s.target[kPedal][static_cast<std::size_t>(w)] += drawbarAmplitude(p.pedal[static_cast<std::size_t>(f)]);
                }
                continue;
            }
            for (int f = 0; f < kDrawbarCount; ++f) {
                float amp = drawbarAmplitude(manualDrawbars[static_cast<std::size_t>(d)][f]);
                if (d == kUpper && p.percOn >= 0.5f && f == kDrawbarCount - 1) amp = 0.0f;   // percussion silences the 1' drawbar
                const int w = wheelFor(key, kFootageSemitones[static_cast<std::size_t>(f)]);
                if (w >= 0) s.target[static_cast<std::size_t>(d)][static_cast<std::size_t>(w)] += amp;
            }
            if (d == kUpper && p.percOn >= 0.5f) {
                const int w = wheelFor(key, p.percHarmonic >= 0.5f ? kPercussionThird : kPercussionSecond);
                if (w >= 0) s.target[3][static_cast<std::size_t>(w)] += 1.0f;
            }
        }
    }
}

}  // namespace

Parameters clampParameters(const Parameters& in) noexcept
{
    Parameters p = in;
    for (auto& v : p.upper) v = roundClamp(v, 0.0f, 8.0f);
    for (auto& v : p.lower) v = roundClamp(v, 0.0f, 8.0f);
    for (auto& v : p.pedal) v = roundClamp(v, 0.0f, 8.0f);
    p.percOn = roundClamp(p.percOn, 0.0f, 1.0f);
    p.percHarmonic = roundClamp(p.percHarmonic, 0.0f, 1.0f);
    p.percDecay = roundClamp(p.percDecay, 0.0f, 1.0f);
    p.percLevel = roundClamp(p.percLevel, 0.0f, 1.0f);
    p.vibrato = roundClamp(p.vibrato, 0.0f, static_cast<float>(kVibratoModeCount - 1));
    p.vibUpper = roundClamp(p.vibUpper, 0.0f, 1.0f);
    p.vibLower = roundClamp(p.vibLower, 0.0f, 1.0f);
    p.scannerRate = clampf(p.scannerRate, kMinScannerRate, kMaxScannerRate);
    p.click = clampf(p.click, 0.0f, 1.0f);
    p.drive = clampf(p.drive, 0.0f, 1.0f);
    p.tone = clampf(p.tone, 0.0f, 1.0f);
    p.width = clampf(p.width, 0.0f, 1.0f);
    p.volume = clampf(p.volume, 0.0f, 1.0f);
    p.upperChannel = roundClamp(p.upperChannel, 0.0f, 16.0f);
    p.lowerChannel = roundClamp(p.lowerChannel, 0.0f, 16.0f);
    p.pedalChannel = roundClamp(p.pedalChannel, 0.0f, 16.0f);
    return p;
}

int wheelFor(const int key, const int semitones) noexcept
{
    int note = key + semitones;
    while (note > kLastWheelNote) note -= 12;
    while (note < kFirstWheelNote) note += 12;
    return note - kFirstWheelNote;
}

double wheelFrequency(const int wheel) noexcept
{
    return 440.0 * std::pow(2.0, (static_cast<double>(kFirstWheelNote + wheel) - 69.0) / 12.0);
}

bool keyInRange(const int division, const int key) noexcept
{
    return key >= kLowestKey && key <= (division == kPedal ? kHighestPedal : kHighestKey);
}

float drawbarAmplitude(const float position) noexcept
{
    if (position <= 0.0f) return 0.0f;
    return std::pow(10.0f, (std::min(position, 8.0f) - 8.0f) * 3.0f / 20.0f);
}

void resetState(EngineState& s) noexcept
{
    for (auto& row : s.gain) row.fill(0.0f);
    for (auto& row : s.target) row.fill(0.0f);
    for (auto& row : s.keyDown) row.fill(false);
    s.upperHeld = 0;
    s.percEnv = 0.0f;
    s.swell = 1.0f;
    s.rng = 0x9E3779B9u;
    s.clickEnv = 0.0f;
    s.noisePrev = 0.0f;
    s.delay.fill(0.0f);
    s.delayPos = 0;
    s.scanPhase = 0.0;
    s.toneState = {};
    // Wheels start at fixed, spread phases: coherent phases would stack every chord into one sharp peak.
    for (int w = 0; w < kWheelCount; ++w) {
        const double x = w * 0.6180339887498949;
        s.phase[static_cast<std::size_t>(w)] = x - std::floor(x);
    }
    s.started = true;
}

void processBlock(EngineState& s, const Parameters& rawParams, const float sampleRate, const std::uint32_t frames,
                  const MidiEvent* events, const std::uint32_t eventCount, float* outLeft, float* outRight) noexcept
{
    if (!s.started) resetState(s);
    if (sampleRate <= 0.0f) {
        std::fill(outLeft, outLeft + frames, 0.0f);
        std::fill(outRight, outRight + frames, 0.0f);
        return;
    }
    const Parameters p = clampParameters(rawParams);
    const SineTable& table = sineTable();
    const double sr = static_cast<double>(sampleRate);

    std::array<double, kWheelCount> increment {};
    for (int w = 0; w < kWheelCount; ++w) increment[static_cast<std::size_t>(w)] = wheelFrequency(w) / sr;

    const float attack = 1.0f - std::exp(-1.0f / (0.0015f * sampleRate));
    const float release = 1.0f - std::exp(-1.0f / (0.003f * sampleRate));
    const float clickDecay = std::exp(-1.0f / (0.002f * sampleRate));
    const float percSeconds = p.percDecay >= 0.5f ? kPercussionSlowSeconds : kPercussionFastSeconds;
    const float percDecay = std::exp(-6.9078f / (percSeconds * sampleRate));   // ln(1000): -60 dB over the decay time
    const float percLevel = p.percLevel >= 0.5f ? 1.0f : kPercussionSoftLevel;
    const bool percOn = p.percOn >= 0.5f;

    const int mode = static_cast<int>(p.vibrato);
    const bool chorus = mode >= 4;
    const float depthSamples = mode == 0 ? 0.0f : kScannerDepthMs[static_cast<std::size_t>((mode - 1) % 3)] * 0.001f * sampleRate;
    const float routedUpper = p.vibUpper, routedLower = p.vibLower;
    const double scanStep = static_cast<double>(p.scannerRate) / sr;
    const double rightOffset = 0.25 * static_cast<double>(p.width);

    const float driveK = 1.0f + 7.0f * p.drive;
    const bool driveOn = p.drive > 1.0e-4f;
    const float toneCutoff = 1500.0f * std::pow(16000.0f / 1500.0f, p.tone);
    const bool toneOn = p.tone < 0.999f;
    const float toneCoef = 1.0f - std::exp(-2.0f * static_cast<float>(kPi) * std::min(toneCutoff, 0.45f * sampleRate) / sampleRate);

    recomputeTargets(s, p);

    const std::array<int, kDivisionCount> channel = {channelOf(p.upperChannel), channelOf(p.lowerChannel), channelOf(p.pedalChannel)};

    std::uint32_t next = 0;
    for (std::uint32_t i = 0; i < frames; ++i) {
        bool changed = false;
        while (next < eventCount && events[next].frame <= i) {
            const MidiEvent& e = events[next++];
            const std::uint8_t kind = e.status & 0xF0;
            const int ch = e.status & 0x0F;
            const bool on = kind == 0x90 && e.data2 > 0;
            const bool off = kind == 0x80 || (kind == 0x90 && e.data2 == 0);
            if (on || off) {
                for (int d = 0; d < kDivisionCount; ++d) {
                    if (channel[static_cast<std::size_t>(d)] != ch || !keyInRange(d, e.data1)) continue;
                    bool& down = s.keyDown[static_cast<std::size_t>(d)][e.data1];
                    if (on && !down) {
                        if (d == kUpper) {
                            if (s.upperHeld == 0) s.percEnv = 1.0f;   // percussion fires on the first key of a legato run
                            ++s.upperHeld;
                        }
                        down = true; s.clickEnv = 1.0f; changed = true;
                    } else if (off && down) {
                        if (d == kUpper) --s.upperHeld;
                        down = false; s.clickEnv = 1.0f; changed = true;
                    }
                }
            } else if (kind == 0xB0 && (e.data1 == 120 || e.data1 == 123)) {
                for (int d = 0; d < kDivisionCount; ++d) {
                    if (channel[static_cast<std::size_t>(d)] != ch) continue;
                    s.keyDown[static_cast<std::size_t>(d)].fill(false);
                    if (d == kUpper) s.upperHeld = 0;
                    changed = true;
                }
            } else if (kind == 0xB0 && e.data1 == 11) {
                s.swell = static_cast<float>(e.data2) / 127.0f;
            }
        }
        if (changed) recomputeTargets(s, p);

        // Wheels: every one turns; only those with a contact closed (or still fading) are summed.
        float bus[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int w = 0; w < kWheelCount; ++w) {
            const auto wi = static_cast<std::size_t>(w);
            double& ph = s.phase[wi];
            ph += increment[wi];
            if (ph >= 1.0) ph -= 1.0;
            bool any = false;
            for (std::size_t d = 0; d < 4; ++d) {
                float& g = s.gain[d][wi];
                const float t = s.target[d][wi];
                if (g == t) { if (t > 0.0f) any = true; continue; }
                g += (t - g) * (t > g ? attack : release);
                if (t == 0.0f && g < 1.0e-7f) g = 0.0f;
                else if (std::fabs(t - g) < 1.0e-7f) g = t;
                if (g > 0.0f) any = true;
            }
            if (!any) continue;
            const float v = sineOf(table, ph);
            bus[0] += s.gain[0][wi] * v;
            bus[1] += s.gain[1][wi] * v;
            bus[2] += s.gain[2][wi] * v;
            bus[3] += s.gain[3][wi] * v;
        }
        const float percAmp = percOn ? s.percEnv * percLevel : 0.0f;
        s.percEnv *= percDecay;
        if (s.percEnv < 1.0e-6f) s.percEnv = 0.0f;
        const float upperBus = bus[0] + bus[3] * percAmp;
        const float lowerBus = bus[1];

        // Scanner vibrato/chorus acts on the manuals routed to it; the pedals go round it.
        const float routed = upperBus * routedUpper + lowerBus * routedLower;
        const float direct = upperBus * (1.0f - routedUpper) + lowerBus * (1.0f - routedLower) + bus[2];
        s.delay[static_cast<std::size_t>(s.delayPos)] = routed;

        // Key click: a short burst of high-passed noise on every key movement.
        s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
        const float noise = static_cast<float>(static_cast<std::int32_t>(s.rng)) * (1.0f / 2147483648.0f);
        const float click = (noise - s.noisePrev) * s.clickEnv * p.click * 0.25f;
        s.noisePrev = noise;
        s.clickEnv *= clickDecay;
        if (s.clickEnv < 1.0e-5f) s.clickEnv = 0.0f;

        const float level = kMasterGain * s.swell * s.swell;
        float out[2];
        for (int c = 0; c < 2; ++c) {
            float routedOut = routed;
            if (mode != 0) {
                double ph = s.scanPhase + (c == 1 ? rightOffset : 0.0);
                ph -= std::floor(ph);
                const float tri = 4.0f * static_cast<float>(std::fabs(ph - 0.5)) - 1.0f;
                const float d = depthSamples * (1.0f + tri);
                const int whole = static_cast<int>(d);
                const float frac = d - static_cast<float>(whole);
                const int a = (s.delayPos - whole) & (kDelayRing - 1);
                const int b = (s.delayPos - whole - 1) & (kDelayRing - 1);
                const float scanned = s.delay[static_cast<std::size_t>(a)] + frac * (s.delay[static_cast<std::size_t>(b)] - s.delay[static_cast<std::size_t>(a)]);
                routedOut = chorus ? 0.5f * (routed + scanned) : scanned;
            }
            float x = (direct + routedOut) * level + click;
            if (driveOn) x = std::tanh(driveK * x) / driveK;
            if (toneOn) {
                float& z = s.toneState[static_cast<std::size_t>(c)];
                z += toneCoef * (x - z);
                if (std::fabs(z) < 1.0e-18f) z = 0.0f;   // no denormals while idle
                x = z;
            }
            out[c] = x * p.volume;
        }
        s.delayPos = (s.delayPos + 1) & (kDelayRing - 1);
        s.scanPhase += scanStep;
        if (s.scanPhase >= 1.0) s.scanPhase -= 1.0;
        outLeft[i] = out[0];
        outRight[i] = out[1];
    }
}

}  // namespace downspout::wheelhouse
