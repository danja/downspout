#pragma once
#include "generative_common.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace downspout::mosaic {
using downspout::generative::MidiEvent;using downspout::generative::ParamSpec;using downspout::generative::Transport;
inline constexpr int kPoolSize=4,kVoiceCount=16;
enum Param:std::uint32_t{kMode,kDensity,kSliceSize,kGrainSize,kReverseChance,kPitchRange,kStereoSpread,kAttack,kRelease,kSeed,kStatusLoaded,kStatusMissing,kStatusVoices,kParameterCount};
inline constexpr std::array<ParamSpec,kParameterCount>kParameterSpecs{{
{"mode","Trigger mode",0,2,2,true},{"density","Autonomous density",0,1,0.35f},{"slice_size","Slice size",0.01f,1,0.25f},{"grain_size","Grain size",0.002f,0.25f,0.04f},
{"reverse_chance","Reverse chance",0,1,0.15f},{"pitch_range","Pitch range",0,24,7,true},{"stereo_spread","Stereo spread",0,1,0.7f},{"attack","Attack",0.001f,0.2f,0.01f},
{"release","Release",0.001f,0.5f,0.08f},{"seed","Seed",1,65535,8807,true},{"status_loaded","Loaded samples",0,kPoolSize,0,true,true},{"status_missing","Missing sample",0,1,1,true,true},{"status_voices","Active voices",0,kVoiceCount,0,true,true}
}};
struct Sample{std::vector<float>data;std::uint32_t channels=0;double sampleRate=0;};
struct Pool{std::array<Sample,kPoolSize>samples{};};
struct LoadResult{Sample sample;std::string error;};
LoadResult loadWav(const std::string&path);
struct Voice{const Sample*sample=nullptr;double position=0,increment=1;std::uint32_t start=0,end=0;float envelope=0,pan=0;bool reverse=false,active=false,releasing=false;};
struct State{std::array<Voice,kVoiceCount>voices{};std::uint64_t triggerSerial=0;std::int64_t lastAutoStep=-1;int statusLoaded=0,statusVoices=0;bool statusMissing=true;};
void reset(State&)noexcept;
void process(State&,const std::array<float,kParameterCount>&,const Transport&,std::uint32_t,double,const Pool*,const MidiEvent*,std::uint32_t,float*,float*)noexcept;
} // namespace downspout::mosaic
