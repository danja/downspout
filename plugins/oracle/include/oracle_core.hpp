#pragma once
#include "generative_common.hpp"
#include <array>
namespace downspout::oracle {
using downspout::generative::MidiBlock;using downspout::generative::MidiEvent;using downspout::generative::ParamSpec;using downspout::generative::Transport;
enum Param:std::uint32_t{kSmoothing,kOnsetThreshold,kResponseChance,kMinNote,kMaxNote,kChannel,kLevelCc,kBrightnessCc,kDensityCc,kPitchCc,kPassInput,kSeed,kFeedbackGuard,
kStatusLevel,kStatusBrightness,kStatusDensity,kStatusPitchClass,kStatusOnset,kStatusFaults,kParameterCount};
inline constexpr std::array<ParamSpec,kParameterCount>kParameterSpecs{{
{"smoothing","Feature smoothing",0,1,0.72f},{"onset_threshold","Onset threshold",0.001f,1,0.08f},{"response_chance","Response chance",0,1,0.35f},
{"min_note","Minimum note",0,127,48,true},{"max_note","Maximum note",0,127,84,true},{"channel","Response channel",1,16,2,true},
{"level_cc","Level CC",0,127,30,true},{"brightness_cc","Brightness CC",0,127,31,true},{"density_cc","Density CC",0,127,32,true},{"pitch_cc","Pitch-class CC",0,127,33,true},
{"pass_input","Pass MIDI input",0,1,1,true},{"seed","Seed",1,65535,7907,true},{"feedback_guard","Response guard (q)",0.25f,16,2},
{"status_level","Level",0,1,0,false,true},{"status_brightness","Brightness",0,1,0,false,true},{"status_density","Density",0,1,0,false,true},
{"status_pitch","Pitch class",0,11,0,true,true},{"status_onset","Onset",0,1,0,false,true},{"status_faults","Non-finite inputs",0,1000000,0,true,true}
}};
struct State{std::array<float,2048>window{};std::uint32_t write=0;float level=0,brightness=0,density=0,onset=0;int pitchClass=0;std::uint64_t faults=0;int activeNote=-1;double lastResponseQuarter=-1000;};
void reset(State&)noexcept;
MidiBlock process(State&,const std::array<float,kParameterCount>&,const Transport&,std::uint32_t,double,
 const float*left,const float*right,const MidiEvent*,std::uint32_t)noexcept;
} // namespace downspout::oracle
