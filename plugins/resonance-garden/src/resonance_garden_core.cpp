#include "resonance_garden_core.hpp"
#include <algorithm>
#include <cmath>
namespace downspout::resonance_garden {
namespace{float pv(const std::array<float,kParameterCount>&p,Param id){return downspout::generative::clampParam(p[id],kParameterSpecs[id]);}int iv(const std::array<float,kParameterCount>&p,Param id){return static_cast<int>(std::lround(pv(p,id)));}
void tune(Voice&v,int note,double sr,float inharm){v.note=note;const double hz=440.0*std::pow(2.0,(note-69)/12.0)*(1.0+inharm*0.015*(note%7));v.delay=std::clamp<std::uint32_t>(static_cast<std::uint32_t>(sr/std::max(20.0,hz)),2,v.buffer.size()-1);v.envelope=1;}
}
void prepare(State&s,double sr){sr=std::max(8000.0,sr);if(s.sampleRate==sr&&!s.voices[0].buffer.empty())return;s.sampleRate=sr;const auto size=static_cast<std::size_t>(sr/20.0+2);for(auto&v:s.voices){v.buffer.assign(size,0);v.write=0;v.delay=100;v.filtered=0;v.envelope=0;v.note=-1;v.held=false;}}
void reset(State&s)noexcept{for(auto&v:s.voices){std::fill(v.buffer.begin(),v.buffer.end(),0);v.write=0;v.filtered=0;v.envelope=0;v.note=-1;v.held=false;}s.steal=0;s.statusVoices=0;s.statusPeak=0;}
void process(State&s,const std::array<float,kParameterCount>&p,std::uint32_t n,double sr,const float*l,const float*r,float*ol,float*orr,const MidiEvent*events,std::uint32_t count)noexcept{
 if(s.sampleRate!=sr||s.voices[0].buffer.empty()){if(ol)std::fill_n(ol,n,0);if(orr)std::fill_n(orr,n,0);return;}std::uint32_t event=0;s.statusPeak=0;const int limit=iv(p,kVoiceLimit);
 for(std::uint32_t frame=0;frame<n;++frame){while(event<count&&events[event].frame<=frame){auto&e=events[event++];if(e.size>=3){int kind=e.data[0]&0xf0,note=e.data[1]&127;bool on=kind==0x90&&e.data[2]>0;if(on){Voice*voice=nullptr;for(int i=0;i<limit;++i)if(s.voices[i].note<0){voice=&s.voices[i];break;}if(!voice)voice=&s.voices[s.steal++%limit];tune(*voice,note,sr,pv(p,kInharmonicity));voice->held=true;}else if(kind==0x80||kind==0x90)for(int i=0;i<limit;++i)if(s.voices[i].note==note)s.voices[i].held=false;}}
  float dryL=l&&std::isfinite(l[frame])?l[frame]:0,dryR=r&&std::isfinite(r[frame])?r[frame]:0,mono=0.5f*(dryL+dryR),wetL=0,wetR=0;int active=0;
  for(int i=0;i<limit;++i){auto&v=s.voices[i];if(v.note<0){constexpr std::array<int,8>major{{0,2,4,7,9,11,14,16}};tune(v,48+iv(p,kRoot)+major[i],sr,pv(p,kInharmonicity));v.envelope=0.25f;}const auto read=(v.write+v.buffer.size()-v.delay)%v.buffer.size();float delayed=v.buffer[read];const float damp=0.02f+(1-pv(p,kDamping))*0.45f;v.filtered+=damp*(delayed-v.filtered);const float decay=std::exp(-1.0f/static_cast<float>(std::max(1.0,sr*pv(p,kDecay))));const float regen=pv(p,kFreeze)>0.5f?0.9995f:std::min(0.995f,decay*pv(p,kFeedback));float write=mono*pv(p,kExcitation)+v.filtered*regen;if(!std::isfinite(write)||std::fabs(write)<1e-20f)write=0;v.buffer[v.write]=std::clamp(write,-2.0f,2.0f);v.write=(v.write+1)%v.buffer.size();if(!v.held&&pv(p,kFreeze)<0.5f)v.envelope*=decay;if(v.envelope>1e-5f){++active;const float pan=i%2?0.65f:0.35f;wetL+=v.filtered*v.envelope*(1-pan);wetR+=v.filtered*v.envelope*pan;}}
  const float mix=pv(p,kMix);float outL=std::tanh(dryL*(1-mix)+wetL*mix*0.5f),outR=std::tanh(dryR*(1-mix)+wetR*mix*0.5f);if(ol)ol[frame]=outL;if(orr)orr[frame]=outR;s.statusVoices=active;s.statusPeak=std::max(s.statusPeak,std::max(std::fabs(outL),std::fabs(outR)));
 }}
} // namespace downspout::resonance_garden
