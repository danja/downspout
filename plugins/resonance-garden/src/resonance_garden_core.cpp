#include "resonance_garden_core.hpp"
#include <algorithm>
#include <cmath>
namespace downspout::resonance_garden {
namespace{
constexpr int kMajor[]{0,2,4,5,7,9,11},kMinor[]{0,2,3,5,7,8,10},kHarmonicMinor[]{0,2,3,5,7,8,11},kMelodicMinor[]{0,2,3,5,7,9,11},kDorian[]{0,2,3,5,7,9,10},kPhrygian[]{0,1,3,5,7,8,10},kLydian[]{0,2,4,6,7,9,11},kMixolydian[]{0,2,4,5,7,9,10},kLocrian[]{0,1,3,5,6,8,10},kPhrygianDominant[]{0,1,4,5,7,8,10},kNeapolitanMajor[]{0,1,3,5,7,9,11},kNeapolitanMinor[]{0,1,3,5,7,8,11},kPentMajor[]{0,2,4,7,9},kPentMinor[]{0,3,5,7,10},kBlues[]{0,3,5,6,7,10},kWholeTone[]{0,2,4,6,8,10},kAltered[]{0,1,3,4,6,8,10},kHalfWholeDim[]{0,1,3,4,6,7,9,10},kWholeHalfDim[]{0,2,3,5,6,8,9,11},kBebopDominant[]{0,2,4,5,7,9,10,11},kBebopMajor[]{0,2,4,5,7,8,9,11},kBebopMinor[]{0,2,3,4,5,7,9,10};
struct Scale{const char*name;const int*steps;int size;};
#define RG_SCALE(n,a) {n,a,static_cast<int>(sizeof(a)/sizeof(int))}
constexpr Scale kScales[kScaleCount]{RG_SCALE("Major",kMajor),RG_SCALE("Ionian",kMajor),RG_SCALE("Minor",kMinor),RG_SCALE("Harmonic Minor",kHarmonicMinor),RG_SCALE("Melodic Minor",kMelodicMinor),RG_SCALE("Dorian",kDorian),RG_SCALE("Phrygian",kPhrygian),RG_SCALE("Lydian",kLydian),RG_SCALE("Mixolydian",kMixolydian),RG_SCALE("Locrian",kLocrian),RG_SCALE("Phrygian Dominant",kPhrygianDominant),RG_SCALE("Neapolitan Major",kNeapolitanMajor),RG_SCALE("Neapolitan Minor",kNeapolitanMinor),RG_SCALE("Pentatonic Major",kPentMajor),RG_SCALE("Pentatonic Minor",kPentMinor),RG_SCALE("Blues",kBlues),RG_SCALE("Whole Tone",kWholeTone),RG_SCALE("Altered",kAltered),RG_SCALE("Half-Whole Diminished",kHalfWholeDim),RG_SCALE("Whole-Half Diminished",kWholeHalfDim),RG_SCALE("Bebop Dominant",kBebopDominant),RG_SCALE("Bebop Major",kBebopMajor),RG_SCALE("Bebop Minor",kBebopMinor)};
#undef RG_SCALE
const Scale&scaleAt(int i){return kScales[std::clamp(i,0,kScaleCount-1)];}
}
const char*scaleName(int i)noexcept{return scaleAt(i).name;}
int scaleSize(int i)noexcept{return scaleAt(i).size;}
const int*scaleIntervals(int i)noexcept{return scaleAt(i).steps;}
int fallbackNote(int root,int scale,int voice)noexcept{const auto&sc=scaleAt(scale);const int v=std::max(0,voice);return std::clamp(48+root+sc.steps[v%sc.size]+12*(v/sc.size),0,127);}
namespace{float pv(const std::array<float,kParameterCount>&p,Param id){return downspout::generative::clampParam(p[id],kParameterSpecs[id]);}int iv(const std::array<float,kParameterCount>&p,Param id){return static_cast<int>(std::lround(pv(p,id)));}
void tune(Voice&v,int note,double sr,float inharm){v.note=note;const double hz=440.0*std::pow(2.0,(note-69)/12.0)*(1.0+inharm*0.015*(note%7));v.delay=std::clamp<std::uint32_t>(static_cast<std::uint32_t>(sr/std::max(20.0,hz)),2,v.buffer.size()-1);v.envelope=1;}
}
void prepare(State&s,double sr){sr=std::max(8000.0,sr);if(s.sampleRate==sr&&!s.voices[0].buffer.empty())return;s.sampleRate=sr;const auto size=static_cast<std::size_t>(sr/20.0+2);for(auto&v:s.voices){v.buffer.assign(size,0);v.write=0;v.delay=100;v.filtered=0;v.envelope=0;v.note=-1;v.held=false;v.internal=false;}}
void reset(State&s)noexcept{for(auto&v:s.voices){std::fill(v.buffer.begin(),v.buffer.end(),0);v.write=0;v.filtered=0;v.envelope=0;v.note=-1;v.held=false;v.internal=false;}s.steal=0;s.statusVoices=0;s.statusPeak=0;}
void process(State&s,const std::array<float,kParameterCount>&p,std::uint32_t n,double sr,const float*l,const float*r,float*ol,float*orr,const MidiEvent*events,std::uint32_t count)noexcept{
 if(s.sampleRate!=sr||s.voices[0].buffer.empty()){if(ol)std::fill_n(ol,n,0);if(orr)std::fill_n(orr,n,0);return;}std::uint32_t event=0;s.statusPeak=0;const int limit=iv(p,kVoiceLimit);
 for(std::uint32_t frame=0;frame<n;++frame){while(event<count&&events[event].frame<=frame){auto&e=events[event++];if(e.size>=3){int kind=e.data[0]&0xf0,note=e.data[1]&127;bool on=kind==0x90&&e.data[2]>0;if(on){Voice*voice=nullptr;for(int i=0;i<limit&&!voice;++i)if(s.voices[i].note<0)voice=&s.voices[i];for(int i=0;i<limit&&!voice;++i)if(s.voices[i].internal)voice=&s.voices[i];if(!voice)voice=&s.voices[s.steal++%limit];tune(*voice,note,sr,pv(p,kInharmonicity));voice->held=true;voice->internal=false;}else if(kind==0x80||kind==0x90)for(int i=0;i<limit;++i)if(s.voices[i].note==note)s.voices[i].held=false;}}
  bool anyHeld=false;for(int i=0;i<limit;++i)anyHeld|=s.voices[i].held&&!s.voices[i].internal;const int root=iv(p,kRoot),scale=iv(p,kScale);
  float dryL=l&&std::isfinite(l[frame])?l[frame]:0,dryR=r&&std::isfinite(r[frame])?r[frame]:0,mono=0.5f*(dryL+dryR),wetL=0,wetR=0;int active=0;
  for(int i=0;i<limit;++i){auto&v=s.voices[i];if(v.note<0||v.internal||(!anyHeld&&!v.held&&v.envelope<1e-3f)){const int target=fallbackNote(root,scale,i);if(!v.internal||v.note!=target){tune(v,target,sr,pv(p,kInharmonicity));v.envelope=0.25f;v.internal=true;}}const auto read=(v.write+v.buffer.size()-v.delay)%v.buffer.size();float delayed=v.buffer[read];const float damp=0.02f+(1-pv(p,kDamping))*0.45f;v.filtered+=damp*(delayed-v.filtered);const float decay=std::exp(-1.0f/static_cast<float>(std::max(1.0,sr*pv(p,kDecay))));const float regen=pv(p,kFreeze)>0.5f?0.9995f:std::min(0.995f,decay*pv(p,kFeedback));float write=mono*pv(p,kExcitation)+v.filtered*regen;if(!std::isfinite(write)||std::fabs(write)<1e-20f)write=0;v.buffer[v.write]=std::clamp(write,-2.0f,2.0f);v.write=(v.write+1)%v.buffer.size();if(v.internal&&!anyHeld)v.envelope+=(0.25f-v.envelope)*0.001f;else if(!v.held&&pv(p,kFreeze)<0.5f)v.envelope*=decay;if(v.envelope>1e-5f){++active;const float pan=i%2?0.65f:0.35f;wetL+=v.filtered*v.envelope*(1-pan);wetR+=v.filtered*v.envelope*pan;}}
  const float mix=pv(p,kMix);float outL=std::tanh(dryL*(1-mix)+wetL*mix*0.5f),outR=std::tanh(dryR*(1-mix)+wetR*mix*0.5f);if(ol)ol[frame]=outL;if(orr)orr[frame]=outR;s.statusVoices=active;s.statusPeak=std::max(s.statusPeak,std::max(std::fabs(outL),std::fabs(outR)));
 }}
} // namespace downspout::resonance_garden
