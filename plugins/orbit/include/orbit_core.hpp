#pragma once
#include "generative_common.hpp"
#include <array>
#include <vector>
namespace downspout::orbit {
using downspout::generative::ParamSpec;using downspout::generative::Transport;
enum Param:std::uint32_t{kTrajectory,kRate,kDepth,kWidth,kDistance,kDoppler,kSeed,kMix,kStatusPan,kStatusDistance,kParameterCount};
inline constexpr std::array<ParamSpec,kParameterCount>kParameterSpecs{{
{"trajectory","Trajectory",0,3,0,true},{"rate","Cycle length (q)",0.25f,32,8},{"depth","Motion depth",0,1,0.8f},{"width","Mid/side width",0,2,1},
{"distance","Distance",0,1,0.35f},{"doppler","Doppler",0,1,0.15f},{"seed","Seed",1,65535,9109,true},{"mix","Wet/dry",0,1,1},
{"status_pan","Pan position",-1,1,0,false,true},{"status_distance","Effective distance",0,1,0,false,true}
}};
struct State{std::vector<float>delayL,delayR;std::uint32_t write=0;double sampleRate=0;float lowL=0,lowR=0,statusPan=0,statusDistance=0;};
void prepare(State&,double);void reset(State&)noexcept;
void process(State&,const std::array<float,kParameterCount>&,const Transport&,std::uint32_t,double,const float*,const float*,float*,float*)noexcept;
} // namespace downspout::orbit
