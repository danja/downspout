#include "drift_core.hpp"
#include <algorithm>
#include <cmath>

namespace downspout::drift {
namespace {
float pv(const std::array<float,kParameterCount>&p,std::uint32_t i)noexcept{return downspout::generative::clampParam(p[i],kParameterSpecs[i]);}
float targetFor(const int mode,const std::uint64_t seed,const int lane,const std::int64_t step,
 const double phase,const float audio,const float previous)noexcept{
 const double x=static_cast<double>(step)+phase;
 switch(mode){
 case 0:return static_cast<float>(0.5+0.5*std::sin(6.283185307179586*x));
 case 1:return downspout::generative::randomUnit(seed+lane*97,step);
 case 2:{const float delta=(downspout::generative::randomUnit(seed+lane*193,step)-0.5f)*0.34f;return std::clamp(previous+delta,0.0f,1.0f);}
 case 3:{const double chaos=std::sin((x+seed*0.0001)*2.17+std::sin(x*0.731)*4.0);return static_cast<float>(0.5+0.5*chaos);}
 default:return std::clamp(audio,0.0f,1.0f);
 }}
}
void reset(State&state)noexcept{state={};state.lastStep={{-1,-1,-1,-1}};state.value={{0.5f,0.5f,0.5f,0.5f}};state.lastEmit={{-1000,-1000,-1000,-1000}};}
MidiBlock process(State&state,const std::array<float,kParameterCount>&p,const Transport&t,
 const std::uint32_t frames,const double sampleRate,const float audioLevel)noexcept{
 MidiBlock out;state.statusEvents=0;
 if(!t.valid||!t.playing||frames==0){state.havePosition=false;return out;}
 const double qpf=std::clamp(t.bpm,1.0,999.0)/(60.0*std::max(1.0,sampleRate));
 const double start=downspout::generative::absoluteQuarter(t),end=start+frames*qpf;
 if(downspout::generative::isDiscontinuity(state.havePosition,state.previousEnd,start))reset(state);
 const std::uint64_t seed=static_cast<std::uint64_t>(std::lround(pv(p,kSeed)));
 const double minQuarter=std::clamp(t.bpm,1.0,999.0)/(60.0*std::max(1.0f,pv(p,kRateLimit)));
 for(int lane=0;lane<kLaneCount;++lane){
  const double rate=pv(p,laneParam(lane,kRate));
  const double phase=pv(p,laneParam(lane,kPhase));
  const std::int64_t step=static_cast<std::int64_t>(std::floor((start+1e-8)/rate));
  const double boundary=static_cast<double>(step)*rate;
  if(step==state.lastStep[static_cast<std::size_t>(lane)]||boundary>=end||start-state.lastEmit[static_cast<std::size_t>(lane)]<minQuarter)continue;
  state.lastStep[static_cast<std::size_t>(lane)]=step;
  const float target=targetFor(static_cast<int>(std::lround(pv(p,laneParam(lane,kMode)))),seed,lane,step,
   phase,audioLevel,state.value[static_cast<std::size_t>(lane)]);
  const float smoothing=pv(p,laneParam(lane,kSmoothing));
  state.value[static_cast<std::size_t>(lane)]+= (target-state.value[static_cast<std::size_t>(lane)])*(1.0f-smoothing*0.92f);
  const int lo=static_cast<int>(std::lround(pv(p,laneParam(lane,kMinimum))));
  const int hi=static_cast<int>(std::lround(pv(p,laneParam(lane,kMaximum))));
  const int value=std::clamp(static_cast<int>(std::lround(std::min(lo,hi)+state.value[static_cast<std::size_t>(lane)]*std::abs(hi-lo))),0,127);
  const int channel=static_cast<int>(std::lround(pv(p,laneParam(lane,kChannel))));
  const int cc=static_cast<int>(std::lround(pv(p,laneParam(lane,kController))));
  out.push(downspout::generative::frameAt(std::max(start,boundary),start,qpf,frames),downspout::generative::ccStatus(channel),
   static_cast<std::uint8_t>(cc),static_cast<std::uint8_t>(value));
  state.lastEmit[static_cast<std::size_t>(lane)]=start;++state.statusEvents;
 }
 state.havePosition=true;state.previousEnd=end;return out;
}
} // namespace downspout::drift
