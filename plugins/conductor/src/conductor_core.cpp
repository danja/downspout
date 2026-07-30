#include "conductor_core.hpp"
#include <algorithm>
#include <cmath>

namespace downspout::conductor {
namespace {
int iv(const std::array<float,kParameterCount>& p, const Param id) noexcept {
    return static_cast<int>(std::lround(downspout::generative::clampParam(
        p[id], kParameterSpecs[id])));
}
float fv(const std::array<float,kParameterCount>& p, const Param id) noexcept {
    return downspout::generative::clampParam(p[id], kParameterSpecs[id]);
}
int chooseSection(const std::array<float,kParameterCount>& p, const int previous,
                  const std::uint64_t serial) noexcept {
    if (iv(p,kMode) == 0) {
        constexpr std::array<int,8> form {{0,1,1,2,1,3,1,4}};
        return form[serial % form.size()];
    }
    std::array<float,5> weights {{fv(p,kIntroWeight),fv(p,kDevelopWeight),
        fv(p,kBreakWeight),fv(p,kRepriseWeight),fv(p,kCodaWeight)}};
    if (previous >= 0) weights[static_cast<std::size_t>(previous)] *= 0.18f;
    if (previous == 0) weights[1] += 0.5f;
    if (previous == 2) weights[3] += 0.45f;
    float total = 0.0f;
    for (float w : weights) total += w;
    float target = downspout::generative::randomUnit(iv(p,kSeed), serial) * std::max(0.001f,total);
    for (int i=0;i<5;++i) { target -= weights[static_cast<std::size_t>(i)]; if (target <= 0.0f) return i; }
    return 1;
}
int duration(const std::array<float,kParameterCount>& p, const std::uint64_t serial) noexcept {
    return downspout::generative::randomInt(iv(p,kSeed), serial+901,
        iv(p,kMinBars), std::max(iv(p,kMinBars),iv(p,kMaxBars)));
}
void release(State& state, MidiBlock& out, const std::uint32_t frame,
             const int channel) noexcept {
    if (state.activeNote >= 0)
        out.push(frame,downspout::generative::status(false,channel),
                 static_cast<std::uint8_t>(state.activeNote),0);
    state.activeNote=-1;
}
void rebuild(State& state, const std::array<float,kParameterCount>& p,
             const int targetBar) noexcept {
    state.section=-1; state.sectionStartBar=0; state.nextSectionBar=0; state.sectionSerial=0;
    for (int guard=0; guard<4096 && state.nextSectionBar<=targetBar; ++guard) {
        state.sectionStartBar=state.nextSectionBar;
        state.section=chooseSection(p,state.section,state.sectionSerial);
        state.nextSectionBar += duration(p,state.sectionSerial);
        ++state.sectionSerial;
    }
}
void emitSection(State& state, const std::array<float,kParameterCount>& p,
                 MidiBlock& out, const std::uint32_t frame) noexcept {
    const int channel=iv(p,kChannel);
    release(state,out,frame,channel);
    state.activeNote=iv(p,kBaseNote)+state.section;
    out.push(frame,downspout::generative::status(true,channel),
             static_cast<std::uint8_t>(state.activeNote),100);
    constexpr std::array<std::array<int,5>,5> values {{
        {{0,30,28,12,127}},
        {{32,74,68,54,0}},
        {{64,18,20,38,0}},
        {{96,62,58,32,0}},
        {{127,20,42,8,127}},
    }};
    const std::array<int,5> ccs {{iv(p,kSceneCc),iv(p,kDensityCc),iv(p,kEnergyCc),
        iv(p,kMutationCc),iv(p,kResetCc)}};
    for (int i=0;i<5;++i)
        out.push(frame,downspout::generative::ccStatus(channel),
                 static_cast<std::uint8_t>(ccs[static_cast<std::size_t>(i)]),
                 static_cast<std::uint8_t>(values[static_cast<std::size_t>(state.section)]
                    [static_cast<std::size_t>(i)]));
}
}
void reset(State& state) noexcept { state={}; state.section=-1; state.activeNote=-1; }
MidiBlock process(State& state, const std::array<float,kParameterCount>& p,
                  const Transport& t, const std::uint32_t frames, const double sampleRate) noexcept {
    MidiBlock out;
    const int channel=iv(p,kChannel);
    if (!t.valid || !t.playing || frames==0) {
        release(state,out,0,channel); state.wasPlaying=false; state.havePosition=false; return out;
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
        rebuild(state,p,startBar);
        emitSection(state,p,out,0);
    }
    while (static_cast<double>(state.nextSectionBar)*barLength < end-1e-8) {
        const double boundary=static_cast<double>(state.nextSectionBar)*barLength;
        if (boundary >= start-1e-8) {
            state.sectionStartBar=state.nextSectionBar;
            state.section=chooseSection(p,state.section,state.sectionSerial);
            state.nextSectionBar += duration(p,state.sectionSerial);
            ++state.sectionSerial;
            emitSection(state,p,out,downspout::generative::frameAt(boundary,start,qpf,frames));
        } else break;
    }
    state.wasPlaying=true; state.havePosition=true; state.previousEnd=end;
    return out;
}
} // namespace downspout::conductor
