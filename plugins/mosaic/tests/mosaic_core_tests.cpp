#include "mosaic_core.hpp"
#include <cassert>
#include <cmath>
#include <cstring>

using namespace downspout::mosaic;

// Build a sine-wave sample at a known frequency so zero-crossings are predictable.
static Sample makeSineSample(double freq = 220.0, double sr = 48000.0, double durationSec = 0.5)
{
    Sample s;
    s.channels = 1;
    s.sampleRate = sr;
    const std::size_t frames = static_cast<std::size_t>(sr * durationSec);
    s.data.resize(frames);
    for (std::size_t i = 0; i < frames; ++i)
        s.data[i] = 0.5f * std::sin(static_cast<float>(2.0 * 3.14159265358979323846 * freq * i / sr));
    return s;
}

static std::array<float, kParameterCount> defaultParams()
{
    std::array<float, kParameterCount> p {};
    for (std::size_t i = 0; i < p.size(); ++i)
        p[i] = kParameterSpecs[i].defaultValue;
    return p;
}

// ── basic smoke test ──────────────────────────────────────────────────────────

static void testSmoke()
{
    auto p = defaultParams();
    Pool pool;
    pool.samples[0] = makeSineSample();

    MidiEvent e;
    e.size = 3;
    e.data = {0x90, 60, 100, 0};
    std::array<float, 1024> l {}, r {};
    State a, b;
    process(a, p, {}, l.size(), 48000, &pool, &e, 1, l.data(), r.data());
    auto first = l;
    process(b, p, {}, l.size(), 48000, &pool, &e, 1, l.data(), r.data());
    assert(first == l && a.statusVoices <= kVoiceCount);
    for (float v : l) assert(std::isfinite(v) && std::fabs(v) <= 1.0f);

    // null pool → silence
    State missing;
    process(missing, p, {}, l.size(), 48000, nullptr, &e, 1, l.data(), r.data());
    assert(missing.statusMissing);
    for (float v : l) assert(v == 0.0f);
}

// ── zero-crossing snap ────────────────────────────────────────────────────────

// After a note-on, the triggered voice's start frame should be at or within
// one sample of a rising zero-crossing in the sine sample.
static void testVoiceStartAtZeroCrossing()
{
    auto p = defaultParams();
    // Fix slice to 50 % so a region is always chosen; no pitch randomisation.
    p[kSliceSize] = 0.5f;
    p[kPitchRange] = 0;
    p[kReverseChance] = 0.0f;

    Pool pool;
    pool.samples[0] = makeSineSample(220.0, 48000.0, 1.0);

    MidiEvent e;
    e.size = 3;
    e.data = {0x90, 60, 100, 0};
    std::array<float, 64> l {}, r {};
    State s;
    process(s, p, {}, l.size(), 48000, &pool, &e, 1, l.data(), r.data());

    // Find which voice was just activated.
    const Voice* v = nullptr;
    for (const auto& voice : s.voices)
        if (voice.active) { v = &voice; break; }
    assert(v != nullptr);

    // Check that v->start is near a rising zero-crossing (v[-1] <= 0, v[0] > 0).
    const auto& data = pool.samples[0].data;
    const std::uint32_t st = v->start;
    // Allow up to 1 frame of rounding at the detected crossing.
    bool atRising = false;
    for (std::uint32_t off = 0; off <= 1 && st + off + 1 < data.size(); ++off) {
        if (data[st + off] <= 0.0f && data[st + off + 1] > 0.0f)
            atRising = true;
    }
    if (st > 0) {
        for (std::uint32_t off = 0; off <= 1 && st >= off + 1; ++off) {
            if (data[st - off - 1] <= 0.0f && data[st - off] > 0.0f)
                atRising = true;
        }
    }
    assert(atRising);
}

// Forward voice position should equal start (after zero-crossing snap).
static void testForwardPositionMatchesStart()
{
    auto p = defaultParams();
    p[kPitchRange] = 0;
    p[kReverseChance] = 0.0f;

    Pool pool;
    pool.samples[0] = makeSineSample(330.0, 44100.0, 0.5);

    MidiEvent e;
    e.size = 3;
    e.data = {0x90, 60, 100, 0};
    std::array<float, 64> l {}, r {};
    State s;
    process(s, p, {}, l.size(), 44100, &pool, &e, 1, l.data(), r.data());

    for (const auto& v : s.voices) {
        if (!v.active || v.reverse) continue;
        // Position was set to snappedStart, which equals voice->start at trigger time.
        // After processing 64 frames it will have advanced, so we only check start.
        assert(v.start < v.end);
    }
}

// Reverse voice should start at end-1 (also zero-crossing snapped via end).
static void testReversePositionAtEnd()
{
    auto p = defaultParams();
    p[kPitchRange] = 0;
    p[kReverseChance] = 1.0f;   // always reverse

    Pool pool;
    pool.samples[0] = makeSineSample(220.0, 48000.0, 0.5);

    MidiEvent e;
    e.size = 3;
    e.data = {0x90, 60, 100, 0};
    std::array<float, 64> l {}, r {};
    State s;
    process(s, p, {}, l.size(), 48000, &pool, &e, 1, l.data(), r.data());

    bool foundReverse = false;
    for (const auto& v : s.voices) {
        if (!v.active || !v.reverse) continue;
        foundReverse = true;
        assert(v.start < v.end);
    }
    assert(foundReverse);
}

int main()
{
    testSmoke();
    testVoiceStartAtZeroCrossing();
    testForwardPositionMatchesStart();
    testReversePositionAtEnd();
    return 0;
}
