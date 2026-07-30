#pragma once
#include "generative_common.hpp"
#include <array>
namespace downspout::polymeter {
using downspout::generative::MidiBlock;using downspout::generative::ParamSpec;using downspout::generative::Transport;
inline constexpr int kLaneCount=4,kLaneParams=9;
enum LaneParam{kLength=0,kPulses,kRotation,kProbability,kRatchets,kAccent,kNote,kChannel,kPhaseDrift};
inline constexpr std::uint32_t laneParam(int lane,int field){return lane*kLaneParams+field;}
inline constexpr std::uint32_t kGrid=kLaneCount*kLaneParams,kSeed=kGrid+1,kStatusStep=kGrid+2,kStatusEvents=kGrid+3,kParameterCount=kGrid+4;
inline constexpr std::array<ParamSpec,kParameterCount>kParameterSpecs{{
{"l1_length","Lane 1 length",1,32,16,true},{"l1_pulses","Lane 1 pulses",0,32,5,true},{"l1_rotation","Lane 1 rotation",0,31,0,true},{"l1_probability","Lane 1 probability",0,1,1},{"l1_ratchets","Lane 1 ratchets",1,4,1,true},{"l1_accent","Lane 1 accent",0,1,0.7f},{"l1_note","Lane 1 note",0,127,36,true},{"l1_channel","Lane 1 channel",1,16,10,true},{"l1_drift","Lane 1 phase drift",0,1,0},
{"l2_length","Lane 2 length",1,32,15,true},{"l2_pulses","Lane 2 pulses",0,32,4,true},{"l2_rotation","Lane 2 rotation",0,31,2,true},{"l2_probability","Lane 2 probability",0,1,0.9f},{"l2_ratchets","Lane 2 ratchets",1,4,1,true},{"l2_accent","Lane 2 accent",0,1,0.55f},{"l2_note","Lane 2 note",0,127,38,true},{"l2_channel","Lane 2 channel",1,16,10,true},{"l2_drift","Lane 2 phase drift",0,1,0.05f},
{"l3_length","Lane 3 length",1,32,12,true},{"l3_pulses","Lane 3 pulses",0,32,7,true},{"l3_rotation","Lane 3 rotation",0,31,1,true},{"l3_probability","Lane 3 probability",0,1,0.8f},{"l3_ratchets","Lane 3 ratchets",1,4,2,true},{"l3_accent","Lane 3 accent",0,1,0.45f},{"l3_note","Lane 3 note",0,127,42,true},{"l3_channel","Lane 3 channel",1,16,10,true},{"l3_drift","Lane 3 phase drift",0,1,0.02f},
{"l4_length","Lane 4 length",1,32,7,true},{"l4_pulses","Lane 4 pulses",0,32,3,true},{"l4_rotation","Lane 4 rotation",0,31,0,true},{"l4_probability","Lane 4 probability",0,1,0.7f},{"l4_ratchets","Lane 4 ratchets",1,4,1,true},{"l4_accent","Lane 4 accent",0,1,0.35f},{"l4_note","Lane 4 note",0,127,46,true},{"l4_channel","Lane 4 channel",1,16,10,true},{"l4_drift","Lane 4 phase drift",0,1,0.08f},
{"grid","Grid in quarters",0.0625f,2,0.25f},{"seed","Seed",1,65535,6907,true},{"status_step","Current step",0,1048576,0,true,true},{"status_events","Events this block",0,512,0,true,true}
}};
struct State{std::array<int,kLaneCount>active{{-1,-1,-1,-1}};std::int64_t lastStep=-1;bool havePosition=false;double previousEnd=0;int statusEvents=0;};
void reset(State&)noexcept;
MidiBlock process(State&,const std::array<float,kParameterCount>&,const Transport&,std::uint32_t,double)noexcept;
} // namespace downspout::polymeter
