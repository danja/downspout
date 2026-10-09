#include "polymeter_core.hpp"
#include "downspout/cellular_automaton.hpp"
#include <algorithm>
#include <cmath>
namespace downspout::polymeter {
namespace{float pv(const std::array<float,kParameterCount>&p,std::uint32_t i){return downspout::generative::clampParam(p[i],kParameterSpecs[i]);}
int iv(const std::array<float,kParameterCount>&p,std::uint32_t i){return static_cast<int>(std::lround(pv(p,i)));}
void releaseLane(State&s,MidiBlock&o,int lane,std::uint32_t frame,const std::array<float,kParameterCount>&p){if(s.active[lane]>=0){o.push(frame,downspout::generative::status(false,iv(p,laneParam(lane,kChannel))),static_cast<std::uint8_t>(s.active[lane]),0);s.active[lane]=-1;}}
}
bool caCell(int length,int pulses,int rule,std::uint64_t seed,int generation,int index)noexcept{
 length=std::clamp(length,1,32);if(index<0||index>=length||rule<=0)return false;
 std::uint32_t row=0;const float density=static_cast<float>(std::clamp(pulses,0,length))/static_cast<float>(length);
 for(int i=0;i<length;++i)if(downspout::generative::randomUnit(seed,static_cast<std::uint64_t>(i))<density)row|=1u<<i;
 if(pulses>=length)row=length==32?0xffffffffu:((1u<<length)-1u);
 std::array<std::uint8_t,32> cells{};for(int i=0;i<length;++i)cells[static_cast<std::size_t>(i)]=static_cast<std::uint8_t>((row>>i)&1u);
 downspout::ca::evolve(cells.data(),length,rule,std::clamp(generation,0,kCaGenerations-1)); // shared automaton arithmetic
 return cells[static_cast<std::size_t>(index)]!=0;}
void reset(State&s)noexcept{s={};s.active={{-1,-1,-1,-1}};s.lastStep=-1;}
void handleMidi(State&s,std::array<float,kParameterCount>&p,const MidiEvent*events,std::uint32_t count)noexcept{
 if(events==nullptr)return;
 for(std::uint32_t i=0;i<count;++i){const MidiEvent&e=events[i];if(e.size<3||(e.data[0]&0xf0)!=0xb0)continue;
  const int conductor=iv(p,kConductorCh);if(conductor<=0||(e.data[0]&0x0f)+1!=conductor)continue;
  const float unit=static_cast<float>(e.data[2]&0x7f)/127.0f;
  switch(e.data[1]){
   case 21:p[kDensity]=unit;break;
   case 22:p[kEnergy]=unit;break;
   case 23:p[kSeed]=downspout::generative::clampParam(kParameterSpecs[kSeed].minimum+unit*(kParameterSpecs[kSeed].maximum-kParameterSpecs[kSeed].minimum),kParameterSpecs[kSeed]);break;
   case 24:if((e.data[2]&0x7f)==127)s.restartPending=true;break;
   default:break;}}}
MidiBlock process(State&s,const std::array<float,kParameterCount>&p,const Transport&t,std::uint32_t frames,double sr)noexcept{
 MidiBlock out;s.statusEvents=0;if(!t.valid||!t.playing||frames==0){for(int l=0;l<kLaneCount;++l)releaseLane(s,out,l,0,p);s.havePosition=false;s.lastBar=-1;s.restartStep=0;s.restartPending=false;return out;}
 const double qpf=std::clamp(t.bpm,1.0,999.0)/(60.0*std::max(1.0,sr)),start=downspout::generative::absoluteQuarter(t),end=start+frames*qpf,grid=pv(p,kGrid),barQ=downspout::generative::barLengthQuarters(t);
 if(downspout::generative::isDiscontinuity(s.havePosition,s.previousEnd,start)){for(int l=0;l<kLaneCount;++l)releaseLane(s,out,l,0,p);s.lastStep=-1;s.lastBar=-1;s.restartStep=0;s.restartPending=false;}
 std::int64_t step=s.lastStep<0?static_cast<std::int64_t>(std::floor((start+1e-8)/grid)):s.lastStep+1;
 double boundary=s.lastStep<0?start:step*grid;
 while(boundary<end-1e-8){
  const auto baseFrame=downspout::generative::frameAt(boundary,start,qpf,frames);
  // A Conductor reset takes effect at the next bar line: the lanes then read the step counted from there.
  const auto bar=static_cast<std::int64_t>(std::floor((boundary+1e-8)/barQ));
  if(bar!=s.lastBar){s.lastBar=bar;if(s.restartPending){s.restartStep=step;s.restartPending=false;}}
  const std::int64_t lstep=step-s.restartStep;
  for(int lane=0;lane<kLaneCount;++lane){releaseLane(s,out,lane,baseFrame,p);const int length=iv(p,laneParam(lane,kLength)),pulses=std::min(length,iv(p,laneParam(lane,kPulses)));
   const int cycle=static_cast<int>(lstep/std::max(1,length));const int drift=static_cast<int>(std::floor(cycle*pv(p,laneParam(lane,kPhaseDrift))));
   const int position=static_cast<int>((lstep+iv(p,laneParam(lane,kRotation))+drift)%length);const int rule=iv(p,ruleParam(lane));const bool pulse=rule>0?caCell(length,pulses,rule,static_cast<std::uint64_t>(iv(p,kSeed))+lane*131+7,cycle%kCaGenerations,position):pulses>0&&((position*pulses)%length)<pulses;
   if(!pulse||downspout::generative::randomUnit(iv(p,kSeed)+lane*101,step)>pv(p,laneParam(lane,kProbability))*pv(p,kDensity))continue;
   const int ratchets=iv(p,laneParam(lane,kRatchets)),note=iv(p,laneParam(lane,kNote)),channel=iv(p,laneParam(lane,kChannel));
   const int rawVelocity=58+static_cast<int>(pv(p,laneParam(lane,kAccent))*64)+(position==0?8:0);
   const int velocity=std::clamp(static_cast<int>(std::lround(rawVelocity*(0.4+0.6*static_cast<double>(pv(p,kEnergy))))),1,127); // Energy 1 = unchanged
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
