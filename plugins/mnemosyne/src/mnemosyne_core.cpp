#include "mnemosyne_core.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace downspout::mnemosyne {
namespace {
int iv(const std::array<float,kParameterCount>&p,Param id){return static_cast<int>(std::lround(downspout::generative::clampParam(p[id],kParameterSpecs[id])));}
float fv(const std::array<float,kParameterCount>&p,Param id){return downspout::generative::clampParam(p[id],kParameterSpecs[id]);}
void release(State&s,MidiBlock&o,std::uint32_t frame,int ch){for(int i=0;i<s.activeCount;++i)o.push(frame,downspout::generative::status(false,ch),static_cast<std::uint8_t>(s.activeNotes[i]),0);s.activeCount=0;}
Phrase fallback(){Phrase p;p.count=4;p.notes[0]={60,94,0,2};p.notes[1]={64,88,4,2};p.notes[2]={67,92,8,2};p.notes[3]={62,84,12,2};return p;}
void commit(State&s){if(s.capture.count==0)return;s.reservoir[static_cast<std::size_t>(s.writePhrase)]=s.capture;s.writePhrase=(s.writePhrase+1)%kPhraseCapacity;s.phraseCount=std::min(kPhraseCapacity,s.phraseCount+1);s.capture={};s.captureIndex.fill(-1);}
Note transformed(Note n,int transform,int rotate,int reg){
 int pitch=n.note;
 switch(transform){case 1:pitch+=7;break;case 2:pitch=reg-(pitch-reg);break;case 3:pitch+=12;break;case 4:pitch-=12;break;case 5:pitch=reg+(pitch-reg)*-1+7;break;default:break;}
 n.note=static_cast<std::uint8_t>(std::clamp(pitch,0,127));n.step=static_cast<std::uint8_t>((n.step+rotate)%16);return n;
}}
void resetRuntime(State&s)noexcept{s.capture={};s.captureIndex.fill(-1);s.phraseSerial=-1;s.activeCount=0;s.havePosition=false;s.previousEnd=0;}
MidiBlock process(State&s,const std::array<float,kParameterCount>&p,const Transport&t,std::uint32_t frames,double sr,
 const MidiEvent*input,std::uint32_t inputCount)noexcept{
 MidiBlock out;const int ch=iv(p,kChannel),mode=iv(p,kMode);
 if(!t.valid||!t.playing||frames==0){release(s,out,0,ch);s.capture={};s.captureIndex.fill(-1);s.havePosition=false;return out;}
 const double qpf=std::clamp(t.bpm,1.0,999.0)/(60.0*std::max(1.0,sr)),start=downspout::generative::absoluteQuarter(t),end=start+qpf*frames;
 const double phraseLen=downspout::generative::barLengthQuarters(t)*iv(p,kPhraseBars);
 const std::int64_t serial=static_cast<std::int64_t>(std::floor((start+1e-8)/phraseLen));
 if(downspout::generative::isDiscontinuity(s.havePosition,s.previousEnd,start)){release(s,out,0,ch);s.capture={};s.captureIndex.fill(-1);s.phraseSerial=serial;}
 if(s.phraseSerial<0)s.phraseSerial=serial;
 if(serial!=s.phraseSerial){commit(s);release(s,out,0,ch);s.phraseSerial=serial;}
 for(std::uint32_t i=0;i<inputCount;++i){
  const auto&e=input[i];if(iv(p,kPassInput)!=0&&out.count<out.events.size())out.events[out.count++]=e;
  if(e.size<3)continue;const int kind=e.data[0]&0xf0,note=e.data[1]&127;const bool on=kind==0x90&&e.data[2]>0;
  const double q=start+std::min(e.frame,frames-1u)*qpf;const int step=std::clamp(static_cast<int>(std::floor(std::fmod(std::max(0.0,q),phraseLen)/phraseLen*16.0)),0,15);
  if(on&&s.capture.count<kEventCapacity){const int idx=s.capture.count++;s.capture.notes[static_cast<std::size_t>(idx)]={static_cast<std::uint8_t>(note),e.data[2],static_cast<std::uint8_t>(step),1};s.captureIndex[static_cast<std::size_t>(note)]=idx;}
  else if(!on&&s.captureIndex[static_cast<std::size_t>(note)]>=0){auto&n=s.capture.notes[static_cast<std::size_t>(s.captureIndex[static_cast<std::size_t>(note)])];n.duration=static_cast<std::uint8_t>(std::clamp(step-static_cast<int>(n.step),1,15));s.captureIndex[static_cast<std::size_t>(note)]=-1;}
 }
 if(mode!=0){
  const Phrase source=s.phraseCount>0?s.reservoir[static_cast<std::size_t>(downspout::generative::randomInt(iv(p,kSeed),serial,0,s.phraseCount-1))]:fallback();
  const int transform=iv(p,kTransform)==0?downspout::generative::randomInt(iv(p,kSeed),serial+19,0,5):iv(p,kTransform)-1;
  const int rotate=static_cast<int>(std::lround((1.0f-fv(p,kRhythmFidelity))*downspout::generative::randomInt(iv(p,kSeed),serial+29,0,7)));
  const double stepLen=phraseLen/16.0;
  for(int i=0;i<source.count;++i){Note n=transformed(source.notes[static_cast<std::size_t>(i)],transform,rotate,iv(p,kRegister));
   const double q=serial*phraseLen+n.step*stepLen;if(q>=start-1e-8&&q<end-1e-8&&downspout::generative::randomUnit(iv(p,kSeed),serial*67+i)<=fv(p,kContinuity)){
    const auto frame=downspout::generative::frameAt(q,start,qpf,frames);release(s,out,frame,ch);out.push(frame,downspout::generative::status(true,ch),n.note,n.velocity);
    if(s.activeCount<static_cast<int>(s.activeNotes.size()))s.activeNotes[s.activeCount++]=n.note;
   }}
 }
 s.havePosition=true;s.previousEnd=end;return out;
}
std::string serializeReservoir(const State&s){std::ostringstream o;o<<"version=1\ncount="<<s.phraseCount<<"\n";for(int p=0;p<s.phraseCount;++p){const auto&ph=s.reservoir[static_cast<std::size_t>(p)];o<<"phrase="<<static_cast<int>(ph.count);for(int i=0;i<ph.count;++i){const auto&n=ph.notes[static_cast<std::size_t>(i)];o<<','<<static_cast<int>(n.note)<<':'<<static_cast<int>(n.velocity)<<':'<<static_cast<int>(n.step)<<':'<<static_cast<int>(n.duration);}o<<'\n';}return o.str();}
bool deserializeReservoir(const std::string&text,State&s){std::istringstream in(text);std::string line;State copy=s;copy.phraseCount=0;copy.writePhrase=0;bool version=false;while(std::getline(in,line)){if(line=="version=1"){version=true;continue;}if(line.rfind("phrase=",0)!=0)continue;Phrase ph;std::istringstream parts(line.substr(7));std::string item;std::getline(parts,item,',');int declared=std::clamp(std::stoi(item),0,kEventCapacity);while(std::getline(parts,item,',')&&ph.count<declared){int a,b,c,d;if(std::sscanf(item.c_str(),"%d:%d:%d:%d",&a,&b,&c,&d)!=4)return false;ph.notes[ph.count++]={static_cast<std::uint8_t>(std::clamp(a,0,127)),static_cast<std::uint8_t>(std::clamp(b,1,127)),static_cast<std::uint8_t>(std::clamp(c,0,15)),static_cast<std::uint8_t>(std::clamp(d,1,15))};}if(copy.phraseCount<kPhraseCapacity)copy.reservoir[copy.phraseCount++]=ph;}if(!version)return false;copy.writePhrase=copy.phraseCount%kPhraseCapacity;resetRuntime(copy);s=copy;return true;}
} // namespace downspout::mnemosyne
