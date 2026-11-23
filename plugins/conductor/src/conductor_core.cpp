#include "conductor_core.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace downspout::conductor {
namespace {

using downspout::conductor_protocol::BlockType;
using downspout::conductor_protocol::kBlockTypeCount;
using downspout::conductor_protocol::toInt;

int iv(const std::array<float,kParameterCount>& p, const Param id) noexcept
{
    return static_cast<int>(std::lround(downspout::generative::clampParam(
        p[id], kParameterSpecs[id])));
}

float fv(const std::array<float,kParameterCount>& p, const Param id) noexcept
{
    return downspout::generative::clampParam(p[id], kParameterSpecs[id]);
}

int blockCount(const std::array<float,kParameterCount>& p) noexcept
{
    return std::clamp(iv(p,kBlockCount), 1, kMaxBlocks);
}

BlockType typeAt(const std::array<float,kParameterCount>& p, const int index) noexcept
{
    const int count = blockCount(p);
    return downspout::conductor_protocol::fromInt(
        iv(p, blockTypeParam(std::clamp(index, 0, count - 1))));
}

float weightFor(const std::array<float,kParameterCount>& p, const BlockType type) noexcept
{
    switch (type) {
    case BlockType::Null:    return fv(p,kNullWeight);
    case BlockType::Intro:   return fv(p,kIntroWeight);
    case BlockType::Theme:   return fv(p,kThemeWeight);
    case BlockType::Develop: return fv(p,kDevelopWeight);
    case BlockType::Break:   return fv(p,kBreakWeight);
    case BlockType::Reprise: return fv(p,kRepriseWeight);
    case BlockType::Coda:    return fv(p,kCodaWeight);
    }
    return 0.0f;
}

struct BlockChoice {
    int index = 0;
    BlockType type = BlockType::Null;
};

BlockChoice fixedChoice(const std::array<float,kParameterCount>& p,
                        const std::uint64_t serial) noexcept
{
    const int count = blockCount(p);
    const int index = static_cast<int>(serial % static_cast<std::uint64_t>(count));
    return {index, typeAt(p, index)};
}

// Weighted mode treats the block list as a palette rather than a sequence: a
// repeated type keeps one weight, so editing a block still changes what can be
// emitted without accidentally making duplicates more likely.
BlockChoice weightedChoice(const std::array<float,kParameterCount>& p,
                           const int previousType, const std::uint64_t serial) noexcept
{
    const int count = blockCount(p);
    std::array<int, kBlockTypeCount> firstIndex;
    firstIndex.fill(-1);
    for (int i = 0; i < count; ++i) {
        const int type = toInt(typeAt(p, i));
        if (firstIndex[static_cast<std::size_t>(type)] < 0)
            firstIndex[static_cast<std::size_t>(type)] = i;
    }

    std::array<float, kBlockTypeCount> weights {};
    bool anyPositive = false;
    for (int t = 0; t < kBlockTypeCount; ++t) {
        if (firstIndex[static_cast<std::size_t>(t)] < 0)
            continue;
        float w = weightFor(p, downspout::conductor_protocol::fromInt(t));
        if (t == previousType)
            w *= 0.18f;
        if (previousType == toInt(BlockType::Intro) && t == toInt(BlockType::Develop))
            w += 0.5f;
        if (previousType == toInt(BlockType::Break) && t == toInt(BlockType::Reprise))
            w += 0.45f;
        weights[static_cast<std::size_t>(t)] = std::max(0.0f, w);
        anyPositive = anyPositive || weights[static_cast<std::size_t>(t)] > 0.0f;
    }

    if (!anyPositive) {
        for (int t = 0; t < kBlockTypeCount; ++t) {
            if (firstIndex[static_cast<std::size_t>(t)] >= 0)
                weights[static_cast<std::size_t>(t)] = 1.0f;
        }
    }

    float total = 0.0f;
    for (float w : weights)
        total += w;
    const float target = downspout::generative::randomUnit(iv(p,kSeed), serial)
        * std::max(0.001f, total);
    float remaining = target;
    for (int t = 0; t < kBlockTypeCount; ++t) {
        const float w = weights[static_cast<std::size_t>(t)];
        if (w <= 0.0f)
            continue;
        remaining -= w;
        if (remaining <= 0.0f)
            return {firstIndex[static_cast<std::size_t>(t)],
                    downspout::conductor_protocol::fromInt(t)};
    }
    for (int t = 0; t < kBlockTypeCount; ++t) {
        if (firstIndex[static_cast<std::size_t>(t)] >= 0)
            return {firstIndex[static_cast<std::size_t>(t)],
                    downspout::conductor_protocol::fromInt(t)};
    }
    return {0, BlockType::Null};
}

BlockChoice chooseBlock(const std::array<float,kParameterCount>& p,
                        const int previousType, const std::uint64_t serial) noexcept
{
    if (iv(p,kMode) == 0)
        return fixedChoice(p, serial);
    return weightedChoice(p, previousType, serial);
}

int duration(const std::array<float,kParameterCount>& p, const std::uint64_t serial) noexcept
{
    return downspout::generative::randomInt(iv(p,kSeed), serial+901,
        iv(p,kMinBars), std::max(iv(p,kMinBars),iv(p,kMaxBars)));
}

void release(State& state, MidiBlock& out, const std::uint32_t frame,
             const int fallbackChannel) noexcept
{
    if (state.activeNote >= 0) {
        const int channel = state.activeChannel >= 0 ? state.activeChannel : fallbackChannel;
        out.push(frame, downspout::generative::status(false,channel),
                 static_cast<std::uint8_t>(state.activeNote),0);
    }
    state.activeNote=-1;
    state.activeChannel=-1;
}

void emitBlock(State& state, const std::array<float,kParameterCount>& p,
               const BlockChoice& choice, MidiBlock& out, const std::uint32_t frame) noexcept
{
    const int channel = iv(p,kChannel);
    release(state,out,frame,channel);
    state.section = toInt(choice.type);
    state.blockIndex = choice.index;

    const int offset = downspout::conductor_protocol::noteOffset(choice.type);
    if (offset >= 0) {
        state.activeNote = iv(p,kBaseNote) + offset;
        state.activeChannel = channel;
        out.push(frame, downspout::generative::status(true,channel),
                 static_cast<std::uint8_t>(state.activeNote),100);
    }

    const auto values = downspout::conductor_protocol::controlValues(choice.type);
    const std::array<int,5> ccs {{iv(p,kSceneCc),iv(p,kDensityCc),iv(p,kEnergyCc),
        iv(p,kMutationCc),iv(p,kResetCc)}};
    const std::array<int,5> sent {{values.scene, values.density, values.energy,
        values.mutation, values.reset}};
    for (int i=0;i<5;++i)
        out.push(frame,downspout::generative::ccStatus(channel),
                 static_cast<std::uint8_t>(ccs[static_cast<std::size_t>(i)]),
                 static_cast<std::uint8_t>(sent[static_cast<std::size_t>(i)]));
}

void rebuild(State& state, const std::array<float,kParameterCount>& p,
             const int targetBar) noexcept
{
    state.section=-1; state.blockIndex=-1; state.sectionStartBar=0;
    state.nextSectionBar=0; state.sectionSerial=0;
    for (int guard=0; guard<4096 && state.nextSectionBar<=targetBar; ++guard) {
        state.sectionStartBar=state.nextSectionBar;
        const BlockChoice choice = chooseBlock(p, state.section, state.sectionSerial);
        state.section = toInt(choice.type);
        state.blockIndex = choice.index;
        state.nextSectionBar += duration(p,state.sectionSerial);
        ++state.sectionSerial;
    }
}

void applyPending(State& state, const std::array<float,kParameterCount>& p,
                  const bool recomputeNext) noexcept
{
    if (state.pendingSection < 0)
        return;
    const int index=std::clamp(state.pendingSection,0,blockCount(p)-1);
    state.section=toInt(typeAt(p,index));
    state.blockIndex=index;
    state.pendingSection=-1;
    state.pendingBar=-1;
    if (recomputeNext && state.sectionSerial > 0)
        state.nextSectionBar=state.sectionStartBar+duration(p,state.sectionSerial-1);
}

} // namespace

void reset(State& state) noexcept
{
    state={};
    state.section=-1;
    state.blockIndex=-1;
    state.activeNote=-1;
    state.activeChannel=-1;
    state.pendingSection=-1;
    state.pendingBar=-1;
}

MidiBlock process(State& state, const std::array<float,kParameterCount>& p,
                  const Transport& t, const std::uint32_t frames, const double sampleRate) noexcept
{
    MidiBlock out;
    const int channel=iv(p,kChannel);
    if (!t.valid || !t.playing || frames==0) {
        release(state,out,0,channel);
        state.wasPlaying=false;
        state.havePosition=false;
        return out;
    }
    const double qpf=std::clamp(t.bpm,1.0,999.0)/(60.0*std::max(1.0,sampleRate));
    const double start=downspout::generative::absoluteQuarter(t);
    const double end=start+frames*qpf;
    const double barLength=downspout::generative::barLengthQuarters(t);
    const int startBar=static_cast<int>(std::floor((start+1e-8)/barLength));
    const bool jump=!state.wasPlaying
        || downspout::generative::isDiscontinuity(state.havePosition,state.previousEnd,start);
    if (jump) {
        release(state,out,0,channel);
        if (state.pendingBar >= 0 && state.pendingBar < startBar)
            state.pendingBar = startBar;
        rebuild(state,p,startBar);
        if (state.pendingSection >= 0 &&
            (state.pendingBar < 0 || state.pendingBar <= startBar)) {
            state.sectionStartBar = startBar;
            applyPending(state,p,true);
        }
        MidiBlock initial;
        emitBlock(state,p,{state.blockIndex,downspout::conductor_protocol::fromInt(state.section)},
                  initial,0);
        for (std::uint32_t i=0;i<initial.count && out.count<out.events.size();++i)
            out.events[out.count++]=initial.events[i];
    }

    while (true) {
        const double sectionBoundary=static_cast<double>(state.nextSectionBar)*barLength;
        double pendingBoundary=std::numeric_limits<double>::infinity();
        if (state.pendingSection >= 0 && state.pendingBar >= 0) {
            pendingBoundary=static_cast<double>(state.pendingBar)*barLength;
            if (pendingBoundary < start-1e-8) {
                state.pendingBar=startBar;
                pendingBoundary=start;
            }
        }
        const double boundary=std::min(sectionBoundary,pendingBoundary);
        if (boundary >= end-1e-8 || boundary < start-1e-8)
            break;
        const bool usePending=state.pendingSection >= 0 && state.pendingBar >= 0
            && pendingBoundary <= sectionBoundary + 1e-8;
        const int eventBar=static_cast<int>(std::floor(boundary/barLength+1e-8));
        state.sectionStartBar=eventBar;
        BlockChoice choice{};
        if (usePending) {
            const int index=std::clamp(state.pendingSection,0,blockCount(p)-1);
            choice={index,typeAt(p,index)};
            state.pendingSection=-1;
            state.pendingBar=-1;
            state.nextSectionBar=eventBar+duration(p,state.sectionSerial);
        } else {
            choice=chooseBlock(p,state.section,state.sectionSerial);
            state.nextSectionBar += duration(p,state.sectionSerial);
        }
        ++state.sectionSerial;
        emitBlock(state,p,choice,out,
                  downspout::generative::frameAt(boundary,start,qpf,frames));
    }
    state.wasPlaying=true; state.havePosition=true; state.previousEnd=end; state.currentBar=startBar;
    return out;
}

} // namespace downspout::conductor
