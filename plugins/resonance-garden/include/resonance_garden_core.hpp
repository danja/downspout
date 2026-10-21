#pragma once
#include "generative_common.hpp"
#include <array>
#include <vector>
namespace downspout::resonance_garden {
using downspout::generative::MidiEvent;using downspout::generative::ParamSpec;
inline constexpr int kMaxVoices=12;
// Internal scales, in BassGen's order (docs/scales.md); append only, the index is saved with the project.
inline constexpr int kScaleCount=23;
enum Param:std::uint32_t{kDecay,kExcitation,kInharmonicity,kDamping,kFreeze,kMix,kFeedback,kRoot,kScale,kVoiceLimit,kStatusVoices,kStatusPeak,kParameterCount};
inline constexpr std::array<ParamSpec,kParameterCount>kParameterSpecs{{
{"decay","Decay",0.05f,12,3},{"excitation","Excitation",0,1,0.65f},{"inharmonicity","Inharmonicity",0,1,0.08f},{"damping","Damping",0,1,0.55f},
{"freeze","Freeze",0,1,0,true},{"mix","Wet/dry",0,1,0.7f},{"feedback","Feedback safety",0,0.98f,0.72f},{"root","Internal root",0,11,0,true},
{"scale","Internal scale",0,kScaleCount-1,0,true},{"voice_limit","Voice limit",1,kMaxVoices,6,true},{"status_voices","Active voices",0,kMaxVoices,0,true,true},{"status_peak","Output peak",0,1.5f,0,false,true}
}};
struct Voice{std::vector<float>buffer;std::uint32_t write=0,delay=100;float filtered=0,envelope=0;int note=-1;bool held=false,internal=false;};
struct State{std::array<Voice,kMaxVoices>voices{};double sampleRate=0;int steal=0,statusVoices=0;float statusPeak=0;};
const char*scaleName(int)noexcept;
// Semitones above the root of each degree of a scale, and how many there are.
int scaleSize(int)noexcept;const int*scaleIntervals(int)noexcept;
// MIDI note the internal bank gives voice `voice`: the scale's degrees climbing from C3 plus the root.
int fallbackNote(int root,int scale,int voice)noexcept;
void prepare(State&,double);void reset(State&)noexcept;
void process(State&,const std::array<float,kParameterCount>&,std::uint32_t,double,const float*,const float*,float*,float*,const MidiEvent*,std::uint32_t)noexcept;
} // namespace downspout::resonance_garden
