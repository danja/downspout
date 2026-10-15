#include "downspout/test_assert.h"

#include "beards_engine.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace downspout::beards;

namespace {

constexpr double kSampleRate = 48000.0;

struct Noise {
    std::uint32_t s = 12345u;
    float next()
    {
        s = s * 1664525u + 1013904223u;
        return (static_cast<float>(s >> 8) / 8388608.0f - 1.0f) * 0.5f;
    }
};

std::unique_ptr<EngineState> makeEngine(const double sr = kSampleRate)
{
    auto state = std::make_unique<EngineState>();
    activate(*state, sr);
    return state;
}

// Renders `seconds` of input (noise at `inputGain`, 0 for silence) and returns the left channel.
std::vector<float> render(EngineState& state, const Parameters& p, const double seconds, const float inputGain,
                          Noise& noise, const double sr = kSampleRate)
{
    const std::size_t frames = static_cast<std::size_t>(seconds * sr);
    std::vector<float> inL(frames), inR(frames), outL(frames), outR(frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        inL[i] = inputGain * noise.next();
        inR[i] = inputGain * noise.next();
    }
    std::size_t done = 0;
    while (done < frames)
    {
        const std::size_t n = std::min<std::size_t>(300, frames - done);
        processBlock(state, p, static_cast<std::uint32_t>(n), inL.data() + done, inR.data() + done,
                     outL.data() + done, outR.data() + done);
        done += n;
    }
    return outL;
}

double energy(const std::vector<float>& v, const std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > v.size())
        to = v.size();
    double e = 0.0;
    for (std::size_t i = from; i < to; ++i)
        e += static_cast<double>(v[i]) * static_cast<double>(v[i]);
    return e;
}

bool differs(const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        d += std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
    return d > 1.0e-3;
}

Parameters wetPatch()
{
    Parameters p;
    p[ParamId::dryWet] = 1.0f;
    // Read grains from ~0.4 s back so a control's effect (feedback needs the wet
    // signal to recirculate through the buffer) shows within a few seconds.
    p[ParamId::position] = 0.1f;
    return p;
}

void midi(Parameters& p, EngineState& s, const int status, const int d1, const int d2)
{
    const std::uint8_t bytes[3] = {static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(d1),
                                   static_cast<std::uint8_t>(d2)};
    applyMidi(p, s, bytes, 3);
}

void testDefaultsAreInRange()
{
    Parameters p;
    for (std::size_t i = 0; i < kParameterCount; ++i)
    {
        const ParamSpec& spec = kParameterSpecs[i];
        assert(spec.defaultValue >= spec.minimum && spec.defaultValue <= spec.maximum);
    }
    // Density must not default into the silent 0.48-0.52 dead zone.
    assert(p[ParamId::density] > 0.52f || p[ParamId::density] < 0.48f);
    // MIDI is opt-in.
    assert(p[ParamId::midiCh] == 0.0f && p[ParamId::conductorCh] == 0.0f);
}

void testSilenceInSilenceOut()
{
    auto s = makeEngine();
    Noise n;
    const auto out = render(*s, Parameters(), 0.5, 0.0f, n);
    assert(energy(out) < 1.0e-12);
}

void testFullyDryPassesTheInput()
{
    auto s = makeEngine();
    Parameters p;
    p[ParamId::dryWet] = 0.0f;
    Noise n;
    const auto out = render(*s, p, 1.0, 1.0f, n);
    // Dry gain is 0.75 (Beads' post gain) and the limiter is transparent at this level:
    // uniform +-0.5 noise has mean square 1/12.
    const double meanSquare = energy(out, 24000) / static_cast<double>(out.size() - 24000);
    const double expected = 0.75 * 0.75 / 12.0;
    assert(meanSquare > 0.8 * expected && meanSquare < 1.2 * expected);
}

void testGrainsProduceWetAudio()
{
    auto s = makeEngine();
    Noise n;
    const Parameters p = wetPatch();
    const auto fill = render(*s, p, 3.0, 1.0f, n);
    assert(energy(fill, 96000) > 1.0e-3);
}

void testFreezeHoldsTheBuffer()
{
    for (const bool frozenFlag : {false, true})
    {
        auto s = makeEngine();
        Noise n;
        Parameters p = wetPatch();
        p[ParamId::position] = 0.1f;
        render(*s, p, 1.0, 1.0f, n);
        p[ParamId::freeze] = frozenFlag ? 1.0f : 0.0f;
        const auto tail = render(*s, p, 6.0, 0.0f, n);
        const double last = energy(tail, tail.size() - 48000);
        if (frozenFlag)
            assert(last > 1.0e-3);
        else
            assert(last < 1.0e-6);
    }
}

void testEveryControlHasAnEffect()
{
    auto baseEngine = makeEngine();
    Noise n0;
    const Parameters base = wetPatch();
    const auto reference = render(*baseEngine, base, 3.0, 1.0f, n0);

    struct Change {
        ParamId id;
        float value;
    };
    const Change changes[] = {
        {ParamId::position, 0.3f}, {ParamId::size, 0.8f},      {ParamId::shape, 0.05f},
        {ParamId::pitch, 7.0f},     {ParamId::density, 0.9f},   {ParamId::feedback, 0.7f},
        {ParamId::dryWet, 0.3f},    {ParamId::reverb, 0.8f},    {ParamId::timeRand, 0.8f},
        {ParamId::sizeRand, 0.8f},  {ParamId::shapeRand, 0.8f}, {ParamId::pitchRand, 0.8f},
        {ParamId::quality, 1.0f},   {ParamId::quality, 2.0f},
    };
    for (const Change& c : changes)
    {
        auto s = makeEngine();
        Noise n;
        Parameters p = base;
        p[c.id] = c.value;
        const auto out = render(*s, p, 3.0, 1.0f, n);
        if (!differs(reference, out))
        {
            std::fprintf(stderr, "control %s has no audible effect\n", kParameterSpecs[static_cast<std::size_t>(c.id)].symbol);
            assert(false);
        }
    }
}

void testFeedbackStaysBounded()
{
    for (const int quality : {0, 1, 2})
    {
        auto s = makeEngine();
        Noise n;
        Parameters p;
        p[ParamId::feedback] = 1.0f;
        p[ParamId::quality] = static_cast<float>(quality);
        p[ParamId::reverb] = 1.0f;
        const auto out = render(*s, p, 8.0, 1.0f, n);
        for (const float v : out)
            assert(std::isfinite(v) && std::fabs(v) < 4.0f);
    }
}

void testOtherSampleRates()
{
    for (const double sr : {44100.0, 96000.0})
    {
        auto s = makeEngine(sr);
        Noise n;
        const auto out = render(*s, wetPatch(), 1.5, 1.0f, n, sr);
        assert(energy(out) > 0.0);
        for (const float v : out)
            assert(std::isfinite(v));
    }
}

void testDriftControllerMap()
{
    auto s = makeEngine();
    Parameters p;

    // Off by default.
    midi(p, *s, 0xB0, 1, 127);
    assert(p[ParamId::position] == 0.5f);

    p[ParamId::midiCh] = 3.0f;
    midi(p, *s, 0xB0, 1, 127);  // channel 1: ignored
    assert(p[ParamId::position] == 0.5f);

    midi(p, *s, 0xB2, 1, 127);  // channel 3
    assert(p[ParamId::position] == 1.0f);
    midi(p, *s, 0xB2, 2, 0);
    assert(p[ParamId::size] == 0.0f);
    midi(p, *s, 0xB2, 3, 127);
    assert(p[ParamId::density] == 1.0f);
    midi(p, *s, 0xB2, 4, 127);
    assert(p[ParamId::dryWet] == 1.0f);
    midi(p, *s, 0xB2, 5, 127);
    assert(p[ParamId::shape] == 1.0f);
    midi(p, *s, 0xB2, 6, 0);
    assert(p[ParamId::pitch] == -24.0f);
    midi(p, *s, 0xB2, 6, 127);
    assert(p[ParamId::pitch] == 24.0f);
    midi(p, *s, 0xB2, 7, 127);
    assert(p[ParamId::feedback] == 1.0f);
    midi(p, *s, 0xB2, 8, 127);
    assert(p[ParamId::reverb] == 1.0f);
    midi(p, *s, 0xB2, 9, 127);
    assert(p[ParamId::pitchRand] == 1.0f);
    midi(p, *s, 0xB2, 10, 127);
    assert(p[ParamId::timeRand] == 1.0f);
    midi(p, *s, 0xB2, 11, 127);
    assert(p[ParamId::sizeRand] == 1.0f);
    midi(p, *s, 0xB2, 12, 127);
    assert(p[ParamId::shapeRand] == 1.0f);
    midi(p, *s, 0xB2, 64, 127);
    assert(p[ParamId::freeze] == 1.0f);
    midi(p, *s, 0xB2, 64, 0);
    assert(p[ParamId::freeze] == 0.0f);

    // Omni.
    p[ParamId::midiCh] = 17.0f;
    midi(p, *s, 0xBA, 1, 0);
    assert(p[ParamId::position] == 0.0f);

    // Non-CC messages are ignored.
    const std::uint8_t note[3] = {0x90, 60, 100};
    assert(!applyMidi(p, *s, note, 3));
}

void testConductorMapping()
{
    auto s = makeEngine();
    Parameters p;
    p[ParamId::conductorCh] = 16.0f;

    midi(p, *s, 0xBF, 21, 127);
    assert(p[ParamId::density] == 1.0f);
    midi(p, *s, 0xBF, 22, 127);
    assert(p[ParamId::dryWet] == 1.0f);
    midi(p, *s, 0xBF, 23, 127);
    assert(p[ParamId::timeRand] == 1.0f && std::fabs(p[ParamId::pitchRand] - 0.5f) < 1.0e-6f);

    // Scene is deliberately unmapped; wrong channel is ignored.
    Parameters before = p;
    midi(p, *s, 0xBF, 20, 64);
    midi(p, *s, 0xBE, 21, 0);
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        assert(p.value[i] == before.value[i]);

    // Drift and Conductor can be received on the same channel without interfering.
    p[ParamId::midiCh] = 16.0f;
    midi(p, *s, 0xBF, 1, 127);
    midi(p, *s, 0xBF, 21, 0);
    assert(p[ParamId::position] == 1.0f && p[ParamId::density] == 0.0f);
}

void testConductorResetFiresAGrain()
{
    for (const bool conductorOn : {false, true})
    {
        auto s = makeEngine();
        Noise n;
        Parameters p = wetPatch();
        p[ParamId::density] = 0.5f;  // dead zone: no automatic grains
        p[ParamId::conductorCh] = conductorOn ? 16.0f : 0.0f;
        render(*s, p, 3.0, 1.0f, n);
        const auto quiet = render(*s, p, 0.2, 1.0f, n);
        assert(energy(quiet) < 1.0e-9);
        assert(activeGrains(*s) == 0);

        midi(p, *s, 0xBF, 24, 100);  // not 127: no trigger
        assert(energy(render(*s, p, 0.1, 1.0f, n)) < 1.0e-9);

        midi(p, *s, 0xBF, 24, 127);
        const auto burst = render(*s, p, 0.2, 1.0f, n);
        if (conductorOn)
            assert(energy(burst) > 1.0e-4);
        else
            assert(energy(burst) < 1.0e-9);
    }
}

void testGrainParameterFiresOnRisingEdge()
{
    auto s = makeEngine();
    Noise n;
    Parameters p = wetPatch();
    p[ParamId::density] = 0.5f;
    render(*s, p, 3.0, 1.0f, n);
    p[ParamId::grain] = 1.0f;
    assert(energy(render(*s, p, 0.2, 1.0f, n)) > 1.0e-4);
    // Held high: the edge does not retrigger, so the single grain dies away.
    render(*s, p, 5.0, 1.0f, n);
    assert(energy(render(*s, p, 0.2, 1.0f, n)) < 1.0e-9);
}

void testHostActivationKeepsThePatch()
{
    auto s = makeEngine();
    Parameters p;
    p[ParamId::position] = 0.23f;
    p[ParamId::midiCh] = 5.0f;
    const Parameters copy = p;
    activate(*s, 44100.0);
    activate(*s, kSampleRate);
    for (std::size_t i = 0; i < kInputParameterCount; ++i)
        assert(p.value[i] == copy.value[i]);
    Noise n;
    assert(energy(render(*s, Parameters(), 0.3, 0.0f, n)) < 1.0e-12);
}

void testClampParameters()
{
    Parameters p;
    p[ParamId::pitch] = 1000.0f;
    p[ParamId::quality] = 7.4f;
    p[ParamId::density] = std::nanf("");
    const Parameters c = clampParameters(p);
    assert(c[ParamId::pitch] == 24.0f && c[ParamId::quality] == 2.0f);
    assert(c[ParamId::density] == kParameterSpecs[static_cast<std::size_t>(ParamId::density)].defaultValue);
}

}  // namespace

int main()
{
    testDefaultsAreInRange();
    testClampParameters();
    testSilenceInSilenceOut();
    testFullyDryPassesTheInput();
    testGrainsProduceWetAudio();
    testFreezeHoldsTheBuffer();
    testEveryControlHasAnEffect();
    testFeedbackStaysBounded();
    testOtherSampleRates();
    testDriftControllerMap();
    testConductorMapping();
    testConductorResetFiresAGrain();
    testGrainParameterFiresOnRisingEdge();
    testHostActivationKeepsThePatch();
    std::puts("beards core tests passed");
    return 0;
}
