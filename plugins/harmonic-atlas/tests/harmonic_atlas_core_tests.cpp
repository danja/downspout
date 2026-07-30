#include "harmonic_atlas_core.hpp"

#include <array>
#include <cassert>

using namespace downspout::harmonic_atlas;

Transport at(const double quarter, const bool playing = true)
{
    Transport t;
    t.valid = true;
    t.playing = playing;
    t.bar = std::floor(quarter / 4.0);
    t.barBeat = quarter - t.bar * 4.0;
    return t;
}

int main()
{
    std::array<float, kParameterCount> p {};
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = kParameterSpecs[i].defaultValue;
    State first;
    State second;
    const auto a = process(first, p, at(0.0), 1024, 48000.0, nullptr, 0);
    const auto b = process(second, p, at(0.0), 1024, 48000.0, nullptr, 0);
    assert(a.count == b.count && a.count >= 4 && a.count <= 7);
    for (std::uint32_t i = 0; i < a.count; ++i) assert(a.events[i].data == b.events[i].data);
    const auto stopped = process(first, p, at(0.1, false), 1024, 48000.0, nullptr, 0);
    assert(stopped.count == static_cast<std::uint32_t>(first.activeCount + a.count) || stopped.count > 0);
    p[kVoiceCount] = 6;
    State bounded;
    const auto chord = process(bounded, p, at(8.0), 1024, 48000.0, nullptr, 0);
    assert(chord.count <= 7);
    State looped;
    const auto loop = process(looped, p, at(0.0), 1024, 48000.0, nullptr, 0);
    assert(loop.count <= 7);
    auto faster = at(4.0);
    faster.bpm = 180.0;
    (void)process(looped, p, faster, 1024, 48000.0, nullptr, 0);
    return 0;
}
