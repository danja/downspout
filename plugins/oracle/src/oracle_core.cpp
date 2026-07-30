#include "oracle_core.hpp"
#include <algorithm>
#include <cmath>
namespace downspout::oracle {
namespace{float pv(const std::array<float,kParameterCount>&p,Param id){return downspout::generative::clampParam(p[id],kParameterSpecs[id]);}int iv(const std::array<float,kParameterCount>&p,Param id){return static_cast<int>(std::lround(pv(p,id)));}}
void reset(State&s)noexcept{s={};s.lastResponseQuarter=-1000;s.activeNote=-1;}
MidiBlock process(State&s,const std::array<float,kParameterCount>&p,const Transport&t,std::uint32_t n,double sr,const float*l,const float*r,const MidiEvent*input,std::uint32_t inputCount)noexcept{
 MidiBlock out;const int ch=iv(p,kChannel);if(s.activeNote>=0){out.push(0,downspout::generative::status(false,ch),static_cast<std::uint8_t>(s.activeNote),0);s.activeNote=-1;}
 double squares=0,difference=0,magnitude=0;int crossings=0;float previous=s.window[(s.write+s.window.size()-1)%s.window.size()];
 for(std::uint32_t i=0;i<n;++i){float a=l?l[i]:0,b=r?r[i]:0;if(!std::isfinite(a)){a=0;++s.faults;}if(!std::isfinite(b)){b=0;++s.faults;}const float mono=0.5f*(a+b);s.window[s.write++%s.window.size()]=mono;squares+=mono*mono;difference+=std::fabs(mono-previous);magnitude+=std::fabs(mono);if((mono>=0)!=(previous>=0))++crossings;previous=mono;}
 const float rawLevel=n?static_cast<float>(std::sqrt(squares/n)):0;const float rawBrightness=static_cast<float>(std::clamp(difference/std::max(1e-9,magnitude),0.0,1.0));
 int noteSum=0,noteCount=0;for(std::uint32_t i=0;i<inputCount;++i){if(iv(p,kPassInput)&&out.count<out.events.size())out.events[out.count++]=input[i];if(input[i].size>=3&&(input[i].data[0]&0xf0)==0x90&&input[i].data[2]>0){noteSum+=input[i].data[1];++noteCount;}}
 const float smooth=pv(p,kSmoothing)*0.96f;s.onset=std::max(0.0f,rawLevel-s.level);s.level=s.level*smooth+rawLevel*(1-smooth);s.brightness=s.brightness*smooth+rawBrightness*(1-smooth);const float rawDensity=std::min(1.0f,noteCount/8.0f);s.density=s.density*smooth+rawDensity*(1-smooth);
 if(noteCount>0)s.pitchClass=(noteSum/noteCount)%12;else if(crossings>1&&n>0){const double hz=crossings*sr/(2.0*n);if(hz>20)s.pitchClass=(static_cast<int>(std::lround(69+12*std::log2(hz/440.0)))%12+12)%12;}
 const std::array<int,4>cc{{iv(p,kLevelCc),iv(p,kBrightnessCc),iv(p,kDensityCc),iv(p,kPitchCc)}};const std::array<int,4>values{{static_cast<int>(s.level*127),static_cast<int>(s.brightness*127),static_cast<int>(s.density*127),s.pitchClass*10}};
 for(int i=0;i<4;++i)out.push(0,downspout::generative::ccStatus(ch),static_cast<std::uint8_t>(cc[i]),static_cast<std::uint8_t>(std::clamp(values[i],0,127)));
 const double quarter=t.valid?downspout::generative::absoluteQuarter(t):0;const bool onset=s.onset>=pv(p,kOnsetThreshold);
 if(onset&&quarter-s.lastResponseQuarter>=pv(p,kFeedbackGuard)&&downspout::generative::randomUnit(iv(p,kSeed),static_cast<std::uint64_t>(quarter*64)+s.faults)<pv(p,kResponseChance)){
  int lo=std::min(iv(p,kMinNote),iv(p,kMaxNote)),hi=std::max(iv(p,kMinNote),iv(p,kMaxNote));int note=lo;while(note%12!=s.pitchClass&&note<hi)++note;s.activeNote=std::clamp(note,lo,hi);out.push(0,downspout::generative::status(true,ch),static_cast<std::uint8_t>(s.activeNote),static_cast<std::uint8_t>(std::clamp(60+static_cast<int>(s.level*60),1,127)));s.lastResponseQuarter=quarter;}
 return out;
}
} // namespace downspout::oracle
