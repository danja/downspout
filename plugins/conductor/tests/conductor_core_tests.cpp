#include "conductor_core.hpp"
#include "downspout/test_assert.h"

#include <array>
#include <cstdint>

using namespace downspout::conductor;
namespace protocol = downspout::conductor_protocol;

namespace {

Transport atBar(const double bar, const bool playing = true)
{
    Transport t;
    t.valid = true;
    t.playing = playing;
    t.bar = bar;
    t.barBeat = 0.0;
    t.beatsPerBar = 4.0;
    t.beatType = 4.0;
    t.bpm = 120.0;
    return t;
}

std::array<float, kParameterCount> defaults()
{
    std::array<float, kParameterCount> p {};
    for (std::size_t i = 0; i < p.size(); ++i)
        p[i] = kParameterSpecs[i].defaultValue;
    return p;
}

int ccValue(const MidiBlock& block, const int cc)
{
    for (std::uint32_t i = 0; i < block.count; ++i) {
        const auto& event = block.events[i];
        if ((event.data[0] & 0xf0) == 0xb0 && event.data[1] == cc)
            return event.data[2];
    }
    return -1;
}

int noteOn(const MidiBlock& block)
{
    for (std::uint32_t i = 0; i < block.count; ++i) {
        const auto& event = block.events[i];
        if ((event.data[0] & 0xf0) == 0x90)
            return event.data[1];
    }
    return -1;
}

int noteOffChannel(const MidiBlock& block)
{
    for (std::uint32_t i = 0; i < block.count; ++i) {
        const auto& event = block.events[i];
        if ((event.data[0] & 0xf0) == 0x80)
            return (event.data[0] & 0x0f) + 1;
    }
    return -1;
}

void setAllWeights(std::array<float, kParameterCount>& p, const float value)
{
    p[kNullWeight] = value;
    p[kIntroWeight] = value;
    p[kThemeWeight] = value;
    p[kDevelopWeight] = value;
    p[kBreakWeight] = value;
    p[kRepriseWeight] = value;
    p[kCodaWeight] = value;
}

void testProtocolTable()
{
    constexpr std::array<int, protocol::kBlockTypeCount> scenes {{1, 0, 16, 32, 64, 96, 127}};
    constexpr std::array<int, protocol::kBlockTypeCount> densities {{0, 30, 48, 74, 18, 62, 20}};
    constexpr std::array<int, protocol::kBlockTypeCount> resets {{0, 127, 0, 0, 0, 0, 127}};
    for (int i = 0; i < protocol::kBlockTypeCount; ++i) {
        const auto type = protocol::fromInt(i);
        assert(protocol::toInt(type) == i);
        assert(protocol::sceneCc(type) == scenes[static_cast<std::size_t>(i)]);
        assert(protocol::controlValues(type).density == densities[static_cast<std::size_t>(i)]);
        assert(protocol::controlValues(type).reset == resets[static_cast<std::size_t>(i)]);
        assert(protocol::blockTypeFromSceneCc(scenes[static_cast<std::size_t>(i)]) == type);
    }
    assert(protocol::blockTypeFromSceneCc(-10) == protocol::BlockType::Intro);
    assert(protocol::blockTypeFromSceneCc(200) == protocol::BlockType::Coda);
    assert(protocol::groundPhraseRoleOverride(protocol::BlockType::Null) == 0);
    assert(protocol::groundPhraseRoleOverride(protocol::BlockType::Intro) == 1);
    assert(protocol::groundPhraseRoleOverride(protocol::BlockType::Theme) == 4);
    assert(protocol::groundPhraseRoleOverride(protocol::BlockType::Develop) == 3);
    assert(protocol::groundPhraseRoleOverride(protocol::BlockType::Break) == 5);
    assert(protocol::groundPhraseRoleOverride(protocol::BlockType::Reprise) == 2);
    assert(protocol::groundPhraseRoleOverride(protocol::BlockType::Coda) == 6);
    assert(protocol::harmonicAtlasStyleFromSceneCc(16) == 1);
    assert(protocol::floozyBodyTypeFromSceneCc(32) == 3);
    assert(protocol::melgenContourFromSceneCc(96) == 4);
    assert(protocol::noteOffset(protocol::BlockType::Null) < 0);
    assert(protocol::noteOffset(protocol::BlockType::Intro) == 0);
    assert(protocol::noteOffset(protocol::BlockType::Coda) == 4);
}

void testFixedFormAndBlockCount()
{
    auto p = defaults();
    p[kMinBars] = 1.0f;
    p[kMaxBars] = 1.0f;
    constexpr std::array<int, 9> expected {{0, 32, 32, 64, 32, 96, 32, 127, 0}};
    State state;
    for (int bar = 0; bar < 9; ++bar) {
        const auto block = process(state, p, atBar(bar), 1024, 48000);
        assert(ccValue(block, 20) == expected[static_cast<std::size_t>(bar)]);
        assert(state.blockIndex == bar % 8);
    }

    auto one = defaults();
    one[kBlockCount] = 0.0f; // clamps to one block
    one[kBlock1Type] = protocol::toInt(protocol::BlockType::Theme);
    one[kMinBars] = 1.0f;
    one[kMaxBars] = 1.0f;
    State single;
    for (int bar = 0; bar < 3; ++bar) {
        const auto block = process(single, one, atBar(bar), 1024, 48000);
        assert(ccValue(block, 20) == 16);
        assert(single.blockIndex == 0);
        assert(noteOn(block) == static_cast<int>(one[kBaseNote]) + 1);
    }

    auto many = defaults();
    many[kBlockCount] = 99.0f; // clamps to eight blocks
    many[kMinBars] = 1.0f;
    many[kMaxBars] = 1.0f;
    State eight;
    for (int bar = 0; bar < 8; ++bar) {
        const auto block = process(eight, many, atBar(bar), 1024, 48000);
        assert(block.count > 0);
        assert(eight.blockIndex == bar);
    }
}

void testNullBlock()
{
    auto p = defaults();
    p[kBlockCount] = 1.0f;
    p[kBlock1Type] = protocol::toInt(protocol::BlockType::Null);
    State state;
    const auto block = process(state, p, atBar(0), 1024, 48000);
    assert(noteOn(block) < 0);
    assert(ccValue(block, 20) == 1);
    assert(ccValue(block, 21) == 0);
    assert(ccValue(block, 24) == 0);
}

void testWeightedPalette()
{
    auto p = defaults();
    p[kMode] = 1.0f;
    p[kBlockCount] = 3.0f;
    p[kBlock1Type] = protocol::toInt(protocol::BlockType::Null);
    p[kBlock2Type] = protocol::toInt(protocol::BlockType::Theme);
    p[kBlock3Type] = protocol::toInt(protocol::BlockType::Break);
    p[kMinBars] = 1.0f;
    p[kMaxBars] = 1.0f;
    setAllWeights(p, 0.0f);

    p[kThemeWeight] = 1.0f;
    State theme;
    for (int bar = 0; bar < 12; ++bar) {
        const auto block = process(theme, p, atBar(bar), 1024, 48000);
        assert(ccValue(block, 20) == 16);
        assert(theme.blockIndex == 1);
    }

    p[kThemeWeight] = 0.0f;
    p[kBreakWeight] = 1.0f;
    State breakState;
    for (int bar = 0; bar < 12; ++bar) {
        const auto block = process(breakState, p, atBar(bar), 1024, 48000);
        assert(ccValue(block, 20) == 64);
        assert(breakState.blockIndex == 2);
    }

    p[kBreakWeight] = 0.0f;
    p[kNullWeight] = 1.0f;
    State nullState;
    for (int bar = 0; bar < 12; ++bar) {
        const auto block = process(nullState, p, atBar(bar), 1024, 48000);
        assert(ccValue(block, 20) == 1);
        assert(noteOn(block) < 0);
        assert(nullState.blockIndex == 0);
    }
}

void testPendingJump()
{
    auto p = defaults();
    p[kMinBars] = 1.0f;
    p[kMaxBars] = 1.0f;
    State state;
    auto first = process(state, p, atBar(0), 1024, 48000);
    assert(ccValue(first, 20) == 0);
    state.pendingSection = 3; // block 4: Break
    state.pendingBar = 1;     // next bar, before the current section ends
    auto second = process(state, p, atBar(1), 1024, 48000);
    assert(ccValue(second, 20) == 64);
    assert(state.blockIndex == 3);
    assert(state.section == protocol::toInt(protocol::BlockType::Break));
    assert(state.pendingSection == -1);
}

void testReleaseChannelAndLifecycle()
{
    auto p = defaults();
    p[kChannel] = 3.0f;
    State state;
    auto start = process(state, p, atBar(0), 1024, 48000);
    assert(noteOn(start) == static_cast<int>(p[kBaseNote]));
    p[kChannel] = 5.0f;
    auto stopped = process(state, p, atBar(1, false), 1024, 48000);
    assert(noteOffChannel(stopped) == 3);

    State a, b;
    auto one = process(a, p, atBar(512), 1024, 48000);
    auto two = process(b, p, atBar(512), 1024, 48000);
    assert(one.count == two.count);
    for (std::uint32_t i = 0; i < one.count; ++i)
        assert(one.events[i].data == two.events[i].data);
}

} // namespace

int main()
{
    testProtocolTable();
    testFixedFormAndBlockCount();
    testNullBlock();
    testWeightedPalette();
    testPendingJump();
    testReleaseChannelAndLifecycle();
    return 0;
}
