#include "guardian_core.hpp"
#include <algorithm>
#include <cmath>
namespace downspout::guardian {
namespace{
float pv(const std::array<float,kParameterCount>&p,Param id){return downspout::generative::clampParam(p[id],kParameterSpecs[id]);}
float db(float value){return std::pow(10.0f,value/20.0f);}
float hp(float x,float&px,float&py,double sr){const float r=static_cast<float>(std::exp(-6.283185307179586*8.0/std::max(1.0,sr)));const float y=x-px+r*py;px=x;py=std::isfinite(y)?y:0;return py;}
float catmull(float p0,float p1,float p2,float p3,float t){const float t2=t*t;return 0.5f*((2.0f*p1)+(-p0+p2)*t+(2.0f*p0-5.0f*p1+4.0f*p2-p3)*t2+(-p0+3.0f*p1-3.0f*p2+p3)*t2*t);}
// Four-phase Catmull-Rom peak of the segment ending one sample ago, plus the
// current sample. tp holds [oldest, ..., newest].
float truePeak4x(float x,float (&tp)[4])noexcept{
 tp[0]=tp[1];tp[1]=tp[2];tp[2]=tp[3];tp[3]=x;
 float peak=std::fabs(x);
 for(int phase=0;phase<4;++phase){const float t=phase*0.25f;peak=std::max(peak,std::fabs(catmull(tp[0],tp[1],tp[2],tp[3],t)));}
 for(float v:tp)peak=std::max(peak,std::fabs(v));
 return peak;
}
// Rebuild a run of clipped samples as a straight line between the clean sample
// that entered it and the first clean sample after it. All other samples are
// returned verbatim. A run longer than the window is restarted at the ceiling
// so the ramp never reaches backwards into already-emitted audio.
float declip(float x,std::vector<float>&ring,std::uint32_t&wr,std::uint32_t window,float&entry,std::uint32_t&run,float ceiling,bool&modified)noexcept{
 const std::uint32_t size=static_cast<std::uint32_t>(ring.size());
 ring[wr]=x;
 const std::uint32_t read=(wr+size-window)%size;
 const float v=ring[read];
 if(std::fabs(v)<=ceiling){entry=v;run=0;modified=false;wr=(wr+1)%size;return v;}
 int exitOffset=-1;float exit=0;
 for(std::uint32_t k=1;k<window;++k){const float f=ring[(read+k)%size];if(std::fabs(f)<=ceiling){exitOffset=static_cast<int>(k);exit=f;break;}}
 float y;
 if(exitOffset>0){const float span=static_cast<float>(run+exitOffset+1);y=entry+(exit-entry)*(static_cast<float>(run+1)/span);++run;}
 else{y=std::clamp(v,-ceiling,ceiling);entry=y;run=0;}
 modified=true;wr=(wr+1)%size;return y;
}
}
std::uint32_t interpFrames(double sr)noexcept{return static_cast<std::uint32_t>(std::max(32.0,static_cast<double>(std::lround(0.005*std::max(1.0,sr)))));}
void prepare(State&s,double sr){sr=std::max(8000.0,sr);const auto size=static_cast<std::size_t>(std::ceil(sr*0.021))+4;const auto isize=interpFrames(sr)+2;if(s.sampleRate!=sr||s.delayL.size()!=size||s.interpL.size()!=isize){s.sampleRate=sr;s.delayL.assign(size,0);s.delayR.assign(size,0);s.interpL.assign(isize,0);s.interpR.assign(isize,0);}resetAudio(s);}
void resetAudio(State&s)noexcept{std::fill(s.delayL.begin(),s.delayL.end(),0);std::fill(s.delayR.begin(),s.delayR.end(),0);std::fill(s.interpL.begin(),s.interpL.end(),0);std::fill(s.interpR.begin(),s.interpR.end(),0);s.write=0;s.interpWriteL=0;s.interpWriteR=0;s.interpEntryL=s.interpEntryR=0;s.interpRunL=s.interpRunR=0;s.interpActive=false;s.gain=1;s.dcXL=s.dcYL=s.dcXR=s.dcYR=0;for(float&v:s.tpL)v=0;for(float&v:s.tpR)v=0;s.limitHold=0;s.autoGainDb=0;s.density=0;s.inputPeak=0;}
std::uint32_t lookaheadFrames(const std::array<float,kParameterCount>&p,double sr)noexcept{return static_cast<std::uint32_t>(std::lround(pv(p,kLookaheadMs)*0.001*std::max(1.0,sr)));}
void process(State&s,const std::array<float,kParameterCount>&p,std::uint32_t n,double sr,const float*l,const float*r,float*ol,float*orr)noexcept{
 if(pv(p,kBypass)>=0.5f){if(ol&&l)std::copy_n(l,n,ol);else if(ol)std::fill_n(ol,n,0);if(orr&&r)std::copy_n(r,n,orr);else if(orr)std::fill_n(orr,n,0);s.density=0;s.inputPeak=0;for(std::uint32_t i=0;i<n;++i){if(l&&std::isfinite(l[i]))s.inputPeak=std::max(s.inputPeak,std::fabs(l[i]));if(r&&std::isfinite(r[i]))s.inputPeak=std::max(s.inputPeak,std::fabs(r[i]));}s.interpActive=false;return;}
 if(s.sampleRate!=sr||s.delayL.empty()||s.interpL.empty()){if(ol)std::fill_n(ol,n,0);if(orr)std::fill_n(orr,n,0);return;}
 const bool autoOn=pv(p,kAutoDrive)>=0.5f,interpOn=pv(p,kInterpolate)>=0.5f,tpOn=pv(p,kTruePeak)>=0.5f,programOn=pv(p,kProgramRelease)>=0.5f;
 if(!autoOn)s.autoGainDb=0;
 if(interpOn!=s.interpActive){s.interpActive=interpOn;std::fill(s.interpL.begin(),s.interpL.end(),0);std::fill(s.interpR.begin(),s.interpR.end(),0);s.interpWriteL=s.interpWriteR=0;s.interpEntryL=s.interpEntryR=0;s.interpRunL=s.interpRunR=0;}
 const bool dcOn=pv(p,kDcRemove)>=0.5f;
 const float inputDb=std::clamp(pv(p,kInputDb)+s.autoGainDb,-24.0f,24.0f);
 const float inputGain=db(inputDb),ceiling=db(pv(p,kCeilingDb));
 const float attackMs=pv(p,kAttackMs);const float attack=attackMs<0.001f?1.0f:1.0f-std::exp(-1.0f/static_cast<float>(std::max(1.0,sr*attackMs*0.001)));
 const float releaseMs=pv(p,kReleaseMs);
 const float release=1.0f-std::exp(-1.0f/static_cast<float>(sr*releaseMs*0.001));
 const float fastRelease=1.0f-std::exp(-1.0f/static_cast<float>(sr*std::max(10.0f,releaseMs*0.25f)*0.001));
 const std::uint32_t shortHold=std::max(1u,static_cast<std::uint32_t>(sr*0.003));
 const float shape=pv(p,kClipperShape);
 const float clipThreshold=ceiling*(1.0f-shape);const float clipRange=ceiling-clipThreshold;
 const float clipK=1.0f+shape*9.0f;const float clipTanhK=shape>0.001f?std::tanh(clipK):1.0f;
 const auto softClip=[&](float v)noexcept->float{
  const float absV=std::fabs(v);if(absV<=clipThreshold)return v;if(clipRange<1e-6f)return std::copysign(ceiling,v);
  const float t=std::min((absV-clipThreshold)/clipRange,1.0f);
  return std::copysign(clipThreshold+clipRange*std::tanh(t*clipK)/clipTanhK,v);};
 const auto latency=std::min<std::size_t>(lookaheadFrames(p,sr),s.delayL.size()-1);
 const std::uint32_t window=interpFrames(sr);
 float minimumGain=1;float blockPeak=0,blockInputPeak=0;std::uint32_t touched=0;
 for(std::uint32_t i=0;i<n;++i){
  float rawL=l?l[i]:0,rawR=r?r[i]:0;
  if(!std::isfinite(rawL)){rawL=0;}if(!std::isfinite(rawR)){rawR=0;}
  blockInputPeak=std::max(blockInputPeak,std::max(std::fabs(rawL),std::fabs(rawR)));
  float a=rawL*inputGain,b=rawR*inputGain;
  if(dcOn){a=hp(a,s.dcXL,s.dcYL,sr);b=hp(b,s.dcXR,s.dcYR,sr);}
  bool modifiedL=false,modifiedR=false;
  if(interpOn){a=declip(a,s.interpL,s.interpWriteL,window,s.interpEntryL,s.interpRunL,ceiling,modifiedL);b=declip(b,s.interpR,s.interpWriteR,window,s.interpEntryR,s.interpRunR,ceiling,modifiedR);}
  s.delayL[s.write]=a;s.delayR[s.write]=b;
  float detected=std::max(std::fabs(a),std::fabs(b));
  if(tpOn)detected=std::max(detected,std::max(truePeak4x(a,s.tpL),truePeak4x(b,s.tpR)));
  else{truePeak4x(a,s.tpL);truePeak4x(b,s.tpR);}
  blockPeak=std::max(blockPeak,detected);
  const float target=detected>ceiling?ceiling/std::max(1e-12f,detected):1.0f;
  const bool limiting=target<0.999f;
  if(limiting)s.limitHold=std::min(s.limitHold+1,shortHold);
  else if(s.gain>=0.999f)s.limitHold=0;
  const float coefficient=target<s.gain?attack:(programOn&&s.gain<0.999f&&s.limitHold<shortHold?fastRelease:release);
  s.gain+=coefficient*(target-s.gain);
  minimumGain=std::min(minimumGain,s.gain);
  const auto read=(s.write+s.delayL.size()-latency)%s.delayL.size();
  float x=s.delayL[read]*s.gain,y=s.delayR[read]*s.gain;s.write=(s.write+1)%s.delayL.size();
  if(shape>0.001f){x=softClip(x);y=softClip(y);}
  if(!std::isfinite(x)){x=0;}if(!std::isfinite(y)){y=0;}
  const bool clippedL=std::fabs(x)>ceiling,clippedR=std::fabs(y)>ceiling;
  x=std::clamp(x,-ceiling,ceiling);y=std::clamp(y,-ceiling,ceiling);
  if(ol){ol[i]=x;}if(orr){orr[i]=y;}
  if(modifiedL||modifiedR||limiting||clippedL||clippedR)++touched;
 }
 const float reductionDb=-20.0f*std::log10(std::max(1e-6f,minimumGain));
 if(autoOn&&blockPeak>1e-5f){
  const float targetReduction=pv(p,kAutoTargetDb);
  const float error=reductionDb-targetReduction;
  const float seconds=static_cast<float>(n)/static_cast<float>(std::max(1.0,sr));
  const float step=std::min(std::fabs(error),seconds*(error>0?8.0f:3.0f));
  s.autoGainDb=std::clamp(s.autoGainDb+(error>0?-step:step),-24.0f,24.0f);
 }
 s.density=n?100.0f*static_cast<float>(touched)/static_cast<float>(n):0;
 s.inputPeak=blockInputPeak;
}
} // namespace downspout::guardian
