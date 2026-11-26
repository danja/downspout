#pragma once
#include "generative_common.hpp"
#include <array>
#include <vector>
namespace downspout::guardian {
using downspout::generative::ParamSpec;
enum Param:std::uint32_t{kInputDb,kCeilingDb,kAttackMs,kReleaseMs,kLookaheadMs,kDcRemove,kTruePeak,kClipperShape,kBypass,
kInterpolate,kAutoDrive,kAutoTargetDb,kProgramRelease,kStatusDensity,kStatusPeak,kParameterCount};
inline constexpr std::array<ParamSpec,kParameterCount>kParameterSpecs{{
{"input_db","Input (dB)",-24,24,0},{"ceiling_db","Ceiling (dB)",-24,-0.1f,-1},{"attack_ms","Attack (ms)",0,50,0},{"release_ms","Release (ms)",10,1000,120},
{"lookahead_ms","Look-ahead (ms)",0,20,5},{"dc_remove","DC removal",0,1,1,true},{"true_peak","4x true-peak guard",0,1,1,true},
{"clipper_shape","Clipper shape",0,1,0},{"bypass","Bypass",0,1,0,true},
{"interpolate","Clip interpolation",0,1,1,true},{"auto_drive","Auto drive",0,1,0,true},{"auto_target_db","Target reduction (dB)",0,12,3},
{"program_release","Program release",0,1,0,true},{"status_density","Protection density (%)",0,100,0,false,true},{"status_input_peak","Input peak",0,4,0,false,true}
}};
// Fixed window used by the input interpolater. Half the memory of the limiter
// delay at 48 kHz, and independent of the user's look-ahead setting.
std::uint32_t interpFrames(double sr)noexcept;
struct State{
 std::vector<float>delayL,delayR;std::uint32_t write=0;double sampleRate=0;float gain=1;
 float dcXL=0,dcYL=0,dcXR=0,dcYR=0;
 float tpL[4]{},tpR[4]{};
 std::vector<float>interpL,interpR;std::uint32_t interpWriteL=0,interpWriteR=0;
 float interpEntryL=0,interpEntryR=0;std::uint32_t interpRunL=0,interpRunR=0;bool interpActive=false;
 std::uint32_t limitHold=0;float autoGainDb=0,density=0,inputPeak=0;
};
void prepare(State&,double);void resetAudio(State&)noexcept;std::uint32_t lookaheadFrames(const std::array<float,kParameterCount>&,double)noexcept;
void process(State&,const std::array<float,kParameterCount>&,std::uint32_t,double,const float*,const float*,float*,float*)noexcept;
} // namespace downspout::guardian
