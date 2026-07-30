#pragma once
#include "generative_common.hpp"
#include <array>
#include <vector>
namespace downspout::guardian {
using downspout::generative::ParamSpec;
enum Param:std::uint32_t{kCeilingDb,kLookaheadMs,kReleaseMs,kDcRemove,kTruePeak,kSoftClip,kSilenceDb,kReset,
kStatusReduction,kStatusFaults,kStatusOverload,kStatusSilence,kStatusPeak,kParameterCount};
inline constexpr std::array<ParamSpec,kParameterCount>kParameterSpecs{{
{"ceiling_db","Ceiling (dB)",-24,-0.1f,-1},{"lookahead_ms","Look-ahead (ms)",0,20,5},{"release_ms","Release (ms)",10,1000,120},
{"dc_remove","DC removal",0,1,1,true},{"true_peak","True-peak guard",0,1,1,true},{"soft_clip","Soft clipping",0,1,1,true},
{"silence_db","Silence threshold",-120,-30,-72},{"reset","Reset diagnostics",0,65535,0,true},
{"status_reduction","Gain reduction (dB)",0,48,0,false,true},{"status_faults","Fault count",0,1000000,0,true,true},
{"status_overload","Overload latched",0,1,0,true,true},{"status_silence","Silence detected",0,1,0,true,true},{"status_peak","True peak",0,4,0,false,true}
}};
struct State{std::vector<float>delayL,delayR;std::uint32_t write=0;double sampleRate=0;float gain=1,dcXL=0,dcYL=0,dcXR=0,dcYR=0,previousL=0,previousR=0;std::uint64_t faults=0;bool overload=false,silence=true;float reductionDb=0,peak=0;int resetSerial=0;};
void prepare(State&,double);void resetAudio(State&)noexcept;std::uint32_t lookaheadFrames(const std::array<float,kParameterCount>&,double)noexcept;
void process(State&,const std::array<float,kParameterCount>&,std::uint32_t,double,const float*,const float*,float*,float*)noexcept;
} // namespace downspout::guardian
