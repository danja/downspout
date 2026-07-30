#pragma once
#include "generative_common.hpp"
#include <array>

namespace downspout::drift {
using downspout::generative::MidiBlock;
using downspout::generative::ParamSpec;
using downspout::generative::Transport;
inline constexpr int kLaneCount=4;
inline constexpr int kLaneParams=8;
enum LaneParam {kMode=0,kRate,kMinimum,kMaximum,kPhase,kSmoothing,kController,kChannel};
inline constexpr std::uint32_t laneParam(const int lane,const int field){return static_cast<std::uint32_t>(lane*kLaneParams+field);}
inline constexpr std::uint32_t kSeed=kLaneCount*kLaneParams;
inline constexpr std::uint32_t kRateLimit=kSeed+1;
inline constexpr std::uint32_t kStatusEvents=kSeed+2;
inline constexpr std::uint32_t kParameterCount=kSeed+3;
inline constexpr std::array<ParamSpec,kParameterCount> kParameterSpecs {{
{"l1_mode","Lane 1 mode",0,4,0,true},{"l1_rate","Lane 1 rate (q)",0.125f,16,1},{"l1_min","Lane 1 minimum",0,127,0,true},{"l1_max","Lane 1 maximum",0,127,127,true},{"l1_phase","Lane 1 phase",0,1,0},{"l1_smoothing","Lane 1 smoothing",0,1,0.25f},{"l1_cc","Lane 1 CC",0,127,1,true},{"l1_channel","Lane 1 channel",1,16,1,true},
{"l2_mode","Lane 2 mode",0,4,1,true},{"l2_rate","Lane 2 rate (q)",0.125f,16,2},{"l2_min","Lane 2 minimum",0,127,24,true},{"l2_max","Lane 2 maximum",0,127,104,true},{"l2_phase","Lane 2 phase",0,1,0.25f},{"l2_smoothing","Lane 2 smoothing",0,1,0.4f},{"l2_cc","Lane 2 CC",0,127,2,true},{"l2_channel","Lane 2 channel",1,16,1,true},
{"l3_mode","Lane 3 mode",0,4,2,true},{"l3_rate","Lane 3 rate (q)",0.125f,16,4},{"l3_min","Lane 3 minimum",0,127,12,true},{"l3_max","Lane 3 maximum",0,127,115,true},{"l3_phase","Lane 3 phase",0,1,0.5f},{"l3_smoothing","Lane 3 smoothing",0,1,0.55f},{"l3_cc","Lane 3 CC",0,127,3,true},{"l3_channel","Lane 3 channel",1,16,1,true},
{"l4_mode","Lane 4 mode",0,4,3,true},{"l4_rate","Lane 4 rate (q)",0.125f,16,8},{"l4_min","Lane 4 minimum",0,127,0,true},{"l4_max","Lane 4 maximum",0,127,127,true},{"l4_phase","Lane 4 phase",0,1,0.75f},{"l4_smoothing","Lane 4 smoothing",0,1,0.65f},{"l4_cc","Lane 4 CC",0,127,4,true},{"l4_channel","Lane 4 channel",1,16,1,true},
{"seed","Seed",1,65535,4049,true},{"rate_limit","Maximum events/sec",1,200,50,true},
{"status_events","Events this block",0,4,0,true,true}
}};
struct State{
 std::array<std::int64_t,kLaneCount>lastStep{{-1,-1,-1,-1}};
 std::array<float,kLaneCount>value{{0.5f,0.5f,0.5f,0.5f}};
 std::array<double,kLaneCount>lastEmit{{-1000,-1000,-1000,-1000}};
 bool havePosition=false;double previousEnd=0;int statusEvents=0;
};
void reset(State&)noexcept;
MidiBlock process(State&,const std::array<float,kParameterCount>&,const Transport&,
 std::uint32_t,double,float audioLevel)noexcept;
} // namespace downspout::drift
