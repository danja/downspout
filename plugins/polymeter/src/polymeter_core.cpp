#include "polymeter_core.hpp"
#include <algorithm>
#include <cmath>
namespace downspout::polymeter {
namespace{float pv(const std::array<float,kParameterCount>&p,std::uint32_t i){return downspout::generative::clampParam(p[i],kParameterSpecs[i]);}
int iv(const std::array<float,kParameterCount>&p,std::uint32_t i){return static_cast<int>(std::lround(pv(p,i)));}
void releaseLane(State&s,MidiBlock&o,int lane,std::uint32_t frame,const std::array<float,kParameterCount>&p){if(s.active[lane]>=0){o.push(frame,downspout::generative::status(false,iv(p,laneParam(lane,kChannel))),static_cast<std::uint8_t>(s.active[lane]),0);s.active[lane]=-1;}}
}
void reset(State&s)noexcept{s={};s.active={{-1,-1,-1,-1}};s.lastStep=-1;}
MidiBlock process(State&s,const std::array<float,kParameterCount>&p,const Transport&t,std::uint32_t frames,double sr)noexcept{
 MidiBlock out;s.statusEvents=0;if(!t.valid||!t.playing||frames==0){for(int l=0;l<kLaneCount;++l)releaseLane(s,out,l,0,p);s.havePosition=false;return out;}
 const double qpf=std::clamp(t.bpm,1.0,999.0)/(60.0*std::max(1.0,sr)),start=downspout::generative::absoluteQuarter(t),end=start+frames*qpf,grid=pv(p,kGrid);
 if(downspout::generative::isDiscontinuity(s.havePosition,s.previousEnd,start)){for(int l=0;l<kLaneCount;++l)releaseLane(s,out,l,0,p);s.lastStep=-1;}
 std::int64_t step=s.lastStep<0?static_cast<std::int64_t>(std::floor((start+1e-8)/grid)):s.lastStep+1;
 double boundary=s.lastStep<0?start:step*grid;
 while(boundary<end-1e-8){
  const auto baseFrame=downspout::generative::frameAt(boundary,start,qpf,frames);
  for(int lane=0;lane<kLaneCount;++lane){releaseLane(s,out,lane,baseFrame,p);const int length=iv(p,laneParam(lane,kLength)),pulses=std::min(length,iv(p,laneParam(lane,kPulses)));
   const int cycle=static_cast<int>(step/std::max(1,length));const int drift=static_cast<int>(std::floor(cycle*pv(p,laneParam(lane,kPhaseDrift))));
   const int position=static_cast<int>((step+iv(p,laneParam(lane,kRotation))+drift)%length);const bool pulse=pulses>0&&((position*pulses)%length)<pulses;
   if(!pulse||downspout::generative::randomUnit(iv(p,kSeed)+lane*101,step)>pv(p,laneParam(lane,kProbability)))continue;
   const int ratchets=iv(p,laneParam(lane,kRatchets)),note=iv(p,laneParam(lane,kNote)),channel=iv(p,laneParam(lane,kChannel));
   const int velocity=std::clamp(58+static_cast<int>(pv(p,laneParam(lane,kAccent))*64)+(position==0?8:0),1,127);
   for(int r=0;r<ratchets;++r){const double rq=boundary+grid*r/ratchets;if(rq>=end)break;const auto frame=downspout::generative::frameAt(rq,start,qpf,frames);
    out.push(frame,downspout::generative::status(true,channel),static_cast<std::uint8_t>(note),static_cast<std::uint8_t>(velocity));
    const double offq=rq+grid*0.45/ratchets;if(offq<end)out.push(downspout::generative::frameAt(offq,start,qpf,frames),downspout::generative::status(false,channel),static_cast<std::uint8_t>(note),0);
    else s.active[lane]=note;
   }
  }
  s.lastStep=step;++step;boundary=step*grid;
 }
 s.statusEvents=static_cast<int>(out.count);s.havePosition=true;s.previousEnd=end;return out;
}
} // namespace downspout::polymeter
