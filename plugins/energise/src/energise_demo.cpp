#include "energise_demo.hpp"

#include "energise_rng.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace downspout::energise {
namespace {

constexpr double kPi = 3.14159265358979323846;

void addSample(std::vector<float>& left, std::vector<float>& right,
               const std::size_t frame, const float value, const float pan)
{
    if (frame >= left.size())
        return;
    left[frame] += value * (1.0f - pan) * 0.5f;
    right[frame] += value * (1.0f + pan) * 0.5f;
}

void addKick(std::vector<float>& left, std::vector<float>& right, const int sr,
             const double start, const float velocity, Rng& rng)
{
    const std::size_t begin = static_cast<std::size_t>(start * sr);
    const std::size_t length = static_cast<std::size_t>(0.45 * sr);
    double phase = 0.0;
    float lp = 0.0f;
    for (std::size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double hz = 48.0 + 130.0 * std::exp(-t * 22.0);
        phase += kPi * 2.0 * hz / sr;
        const float body = static_cast<float>(std::sin(phase) * std::exp(-t * 8.5));
        const float noise = rng.nextSigned();
        lp += 0.06f * (noise - lp);
        const float click = i < static_cast<std::size_t>(0.004 * sr) ? (noise - lp) * 0.65f : 0.0f;
        addSample(left, right, begin + i, (body * 0.85f + click) * velocity, 0.0f);
    }
}

void addSnare(std::vector<float>& left, std::vector<float>& right, const int sr,
              const double start, const float velocity, Rng& rng)
{
    const std::size_t begin = static_cast<std::size_t>(start * sr);
    const std::size_t length = static_cast<std::size_t>(0.32 * sr);
    double phase = 0.0;
    float lp = 0.0f;
    for (std::size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sr;
        phase += kPi * 2.0 * 205.0 / sr;
        const float noise = rng.nextSigned();
        lp += 0.16f * (noise - lp);
        const float bright = noise - lp;
        const float body = static_cast<float>(std::sin(phase) * std::exp(-t * 30.0));
        const float envelope = static_cast<float>(std::exp(-t * 14.0));
        addSample(left, right, begin + i, (bright * 0.75f + body * 0.45f) * envelope * velocity, 0.0f);
    }
}

void addHat(std::vector<float>& left, std::vector<float>& right, const int sr,
            const double start, const float velocity, const double decay, Rng& rng,
            const float pan)
{
    const std::size_t begin = static_cast<std::size_t>(start * sr);
    const std::size_t length = static_cast<std::size_t>(decay * 3.0 * sr);
    float lp = 0.0f;
    for (std::size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sr;
        const float noise = rng.nextSigned();
        lp += 0.35f * (noise - lp);
        const float bright = noise - lp;
        const float envelope = static_cast<float>(std::exp(-t / decay * 3.0));
        addSample(left, right, begin + i, bright * envelope * velocity * 0.7f, pan);
    }
}

void addTom(std::vector<float>& left, std::vector<float>& right, const int sr,
            const double start, const float velocity, Rng& rng)
{
    const std::size_t begin = static_cast<std::size_t>(start * sr);
    const std::size_t length = static_cast<std::size_t>(0.45 * sr);
    double phase = 0.0;
    for (std::size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double hz = 110.0 + 90.0 * std::exp(-t * 15.0);
        phase += kPi * 2.0 * hz / sr;
        const float body = static_cast<float>(std::sin(phase) * std::exp(-t * 9.0));
        const float attack = i < static_cast<std::size_t>(0.006 * sr) ? rng.nextSigned() * 0.3f : 0.0f;
        addSample(left, right, begin + i, (body * 0.8f + attack) * velocity, 0.05f);
    }
}

void addCrash(std::vector<float>& left, std::vector<float>& right, const int sr,
              const double start, const float velocity, Rng& rng)
{
    const std::size_t begin = static_cast<std::size_t>(start * sr);
    const std::size_t length = static_cast<std::size_t>(1.8 * sr);
    static constexpr std::array<double, 8> kPartials = {{
        2103.0, 2789.0, 3647.0, 4211.0, 5317.0, 6199.0, 7531.0, 8807.0,
    }};
    std::array<double, kPartials.size()> phases {};
    float lp = 0.0f;
    for (std::size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sr;
        float metallic = 0.0f;
        for (std::size_t p = 0; p < kPartials.size(); ++p) {
            phases[p] += kPi * 2.0 * kPartials[p] / sr;
            metallic += static_cast<float>(std::sin(phases[p])) / static_cast<float>(kPartials.size());
        }
        const float noise = rng.nextSigned();
        lp += 0.4f * (noise - lp);
        const float envelope = static_cast<float>(std::exp(-t * 1.7));
        addSample(left, right, begin + i, (metallic * 0.8f + (noise - lp) * 0.2f) * envelope * velocity, -0.35f);
    }
}

}  // namespace

CaptureBuffer makeDemoBreak(const int sampleRate, const std::uint32_t seed)
{
    const int sr = sampleRate > 0 ? sampleRate : 48000;
    CaptureBuffer capture {};
    capture.sampleRate = sr;
    capture.left.assign(static_cast<std::size_t>(sr * 6), 0.0f);
    capture.right.assign(capture.left.size(), 0.0f);

    Rng rng {};
    rng.seed(seed == 0u ? 1789u : seed);

    struct Hit {
        double time;
        DrumClass cls;
        float velocity;
    };
    // Two bars at 120 BPM with swing-like offbeats, one overlap and one ghost.
    static constexpr std::array<Hit, 24> kHits = {{
        {0.000, DrumClass::Kick, 1.00f}, {0.250, DrumClass::ClosedHat, 0.50f},
        {0.500, DrumClass::Snare, 0.95f}, {0.750, DrumClass::ClosedHat, 0.45f},
        {1.000, DrumClass::Kick, 0.98f}, {1.250, DrumClass::ClosedHat, 0.52f},
        {1.420, DrumClass::Snare, 0.20f}, {1.500, DrumClass::Snare, 0.92f},
        {1.750, DrumClass::ClosedHat, 0.44f},
        {2.000, DrumClass::Kick, 1.00f}, {2.000, DrumClass::ClosedHat, 0.48f},
        {2.500, DrumClass::Snare, 0.93f}, {2.750, DrumClass::ClosedHat, 0.46f},
        {3.000, DrumClass::Kick, 0.97f}, {3.000, DrumClass::Crash, 0.70f},
        {3.250, DrumClass::LowTom, 0.82f}, {3.500, DrumClass::Snare, 0.94f},
        {3.750, DrumClass::ClosedHat, 0.47f},
        {4.000, DrumClass::Kick, 1.00f}, {4.250, DrumClass::ClosedHat, 0.50f},
        {4.500, DrumClass::Snare, 0.95f}, {4.750, DrumClass::ClosedHat, 0.45f},
        {5.000, DrumClass::Kick, 0.98f}, {5.500, DrumClass::Snare, 0.92f},
    }};

    for (const Hit& hit : kHits) {
        switch (hit.cls) {
        case DrumClass::Kick:
            addKick(capture.left, capture.right, sr, hit.time, hit.velocity, rng);
            break;
        case DrumClass::Snare:
            addSnare(capture.left, capture.right, sr, hit.time, hit.velocity, rng);
            break;
        case DrumClass::ClosedHat:
        case DrumClass::OpenHat:
            addHat(capture.left, capture.right, sr, hit.time, hit.velocity,
                   hit.cls == DrumClass::OpenHat ? 0.35 : 0.055, rng, 0.25f);
            break;
        case DrumClass::LowTom:
        case DrumClass::MidTom:
        case DrumClass::HighTom:
            addTom(capture.left, capture.right, sr, hit.time, hit.velocity, rng);
            break;
        case DrumClass::Crash:
        case DrumClass::Ride:
            addCrash(capture.left, capture.right, sr, hit.time, hit.velocity, rng);
            break;
        default:
            addSnare(capture.left, capture.right, sr, hit.time, hit.velocity, rng);
            break;
        }
    }

    // A quiet broadband floor makes the fixture behave like a real, noisy break.
    for (std::size_t i = 0; i < capture.left.size(); ++i) {
        capture.left[i] += rng.nextSigned() * 0.0006f;
        capture.right[i] += rng.nextSigned() * 0.0006f;
    }
    return capture;
}

}  // namespace downspout::energise
