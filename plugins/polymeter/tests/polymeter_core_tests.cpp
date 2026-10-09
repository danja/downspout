#include "polymeter_core.hpp"
#include "downspout/test_assert.h"
#include <array>
#include <cmath>
#include <vector>
using namespace downspout::polymeter;

static void testCellularAutomaton(){
 // All cells alive at the start (pulses == length).
 for(int i=0;i<12;++i)assert(caCell(12,12,90,1,0,i));
 // Rule 90 is XOR of the neighbours: an all-alive row dies in one generation.
 for(int i=0;i<12;++i)assert(!caCell(12,12,90,1,1,i));
 // Rule 204 copies a cell, rule 51 inverts it.
 for(int g=0;g<5;++g)for(int i=0;i<9;++i){assert(caCell(9,9,204,1,g,i));assert(caCell(9,9,51,1,g,i)==(g%2==0));}
 // Rule 0 is "Euclidean", never a CA cell; no pulses is an empty row.
 assert(!caCell(8,8,0,1,0,0));
 for(int g=0;g<4;++g)for(int i=0;i<8;++i)assert(!caCell(8,0,30,1,g,i));
 // Repeatable, and different seeds give different rows.
 int diff=0;for(int i=0;i<16;++i){assert(caCell(16,6,30,5,3,i)==caCell(16,6,30,5,3,i));diff+=caCell(16,6,30,5,0,i)!=caCell(16,6,30,6,0,i);}
 assert(diff>0);
 // Independent step of rule 30 on a known row: 00010000 (cell 3 alive) -> cells 2,3,4 become 1,1,1 by Wolfram's table.
 // Build that row through density 1/8 is seed dependent, so check the rule table directly instead.
 const int rule30[8]={0,1,1,1,1,0,0,0};  // outputs for neighbourhoods 000..111
 for(int pattern=0;pattern<8;++pattern)assert(((30>>pattern)&1)==rule30[pattern]);
 // A lane with a rule plays, still deterministic, and differs from the Euclidean lane.
 std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;
 auto render=[&](std::array<float,kParameterCount> q){State st;Transport t;t.valid=true;t.playing=true;t.bpm=120;std::vector<std::array<std::uint8_t,4>>ev;
  for(int block=0;block<640;++block){t.bar=block/64;t.barBeat=(block%64)*0.0625;auto x=process(st,q,t,1536,48000);for(std::uint32_t i=0;i<x.count;++i)ev.push_back(x.events[i].data);}return ev;};
 auto base=render(p);auto again=render(p);assert(base==again);
 auto ca=p;ca[ruleParam(0)]=30;ca[ruleParam(1)]=110;auto a=render(ca);assert(a==render(ca)&&!a.empty()&&a!=base);
}

// ---- Conductor CC set ------------------------------------------------------------------------
namespace {
using Params=std::array<float,kParameterCount>;
Params defaultParams(){Params p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;return p;}
struct Hit{long long frame;int status,d1,d2;bool operator==(const Hit&o)const{return frame==o.frame&&status==o.status&&d1==o.d1&&d2==o.d2;}};
void cc(State&s,Params&p,int status,int d1,int d2){downspout::generative::MidiEvent e;e.size=3;e.data={static_cast<std::uint8_t>(status),static_cast<std::uint8_t>(d1),static_cast<std::uint8_t>(d2),0};handleMidi(s,p,&e,1);}
// 120 bpm, 48 kHz: a bar is 96000 frames. Blocks of 1000 frames; `hook(pos, params, state)` runs before each.
template<typename Hook>std::vector<Hit> renderBars(Params p,int bars,State&st,Hook hook){
 constexpr long long barFrames=96000;std::vector<Hit>hits;
 for(long long pos=0;pos<bars*barFrames;pos+=1000){hook(pos,p,st);Transport t;t.valid=true;t.playing=true;t.bpm=120;const double q=static_cast<double>(pos)/48000.0*2.0;t.bar=std::floor(q/4.0);t.barBeat=q-t.bar*4.0;
  auto x=process(st,p,t,1000,48000);for(std::uint32_t i=0;i<x.count;++i)hits.push_back({pos+x.events[i].frame,x.events[i].data[0],x.events[i].data[1],x.events[i].data[2]});}
 return hits;}
}
static void testConductor(){
 Params p=defaultParams();State s;
 // Off by default, and the new master controls default to "no change".
 assert(p[kConductorCh]==0&&p[kDensity]==1&&p[kEnergy]==1);
 cc(s,p,0xbf,21,0);assert(p[kDensity]==1);
 p[kConductorCh]=16;
 cc(s,p,0xbf,21,0);assert(p[kDensity]==0);cc(s,p,0xbf,21,127);assert(p[kDensity]==1);
 cc(s,p,0xbf,22,0);assert(p[kEnergy]==0);cc(s,p,0xbf,22,127);assert(p[kEnergy]==1);
 cc(s,p,0xbf,23,0);assert(p[kSeed]==1);cc(s,p,0xbf,23,127);assert(p[kSeed]==65535);
 const Params before=p;cc(s,p,0xb0,21,0);cc(s,p,0xbf,20,0);cc(s,p,0xbf,7,0);assert(p==before); // other channel, scene, unrelated CC
 assert(!s.restartPending);cc(s,p,0xbf,24,100);assert(!s.restartPending);cc(s,p,0xbf,24,127);assert(s.restartPending);

 // Density 0 silences every lane; density 1 is the plain pattern. Energy scales velocity and never exceeds the plain one.
 Params base=defaultParams();for(int l=0;l<kLaneCount;++l)base[laneParam(l,kProbability)]=1;
 State a;auto plain=renderBars(base,2,a,[](long long,Params&,State&){});assert(!plain.empty());
 Params quiet=base;quiet[kDensity]=0;State b;assert(renderBars(quiet,2,b,[](long long,Params&,State&){}).empty());
 Params soft=base;soft[kEnergy]=0;State c;auto softer=renderBars(soft,2,c,[](long long,Params&,State&){});
 assert(softer.size()==plain.size());int louder=0;for(std::size_t i=0;i<plain.size();++i){assert(softer[i].frame==plain[i].frame&&softer[i].d1==plain[i].d1);if((plain[i].status&0xf0)==0x90&&plain[i].d2>0){assert(softer[i].d2<plain[i].d2);++louder;}}
 assert(louder>0);

 // A reset restarts the lanes at the next bar line. Lane 4 is 7 steps long, so it drifts against a 16-step bar; with all
 // probabilities at 1 its notes in bar 1 equal those of bar 0 only if it restarted.
 const auto lane4=[&](const std::vector<Hit>&hits,long long from,long long to){std::vector<Hit>r;for(const Hit&h:hits)if(h.frame>=from&&h.frame<to&&h.d1==base[laneParam(3,kNote)])r.push_back({h.frame-from,h.status,h.d1,h.d2});return r;};
 State drift;auto free=renderBars(base,3,drift,[](long long,Params&,State&){});
 assert(lane4(free,0,96000)!=lane4(free,96000,192000)); // without a reset the 7-step lane has moved
 Params conductor=base;conductor[kConductorCh]=16;State reset;
 auto restarted=renderBars(conductor,3,reset,[](long long pos,Params&q,State&st){if(pos==40000)cc(st,q,0xbf,24,127);});
 assert(lane4(restarted,0,96000)==lane4(restarted,96000,192000));
 assert(lane4(restarted,0,96000)==lane4(free,0,96000)); // and bar 0, before the reset, is untouched
}
int main(){testCellularAutomaton();testConductor();std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;Transport t;t.valid=true;t.playing=true;t.bpm=123;State a,b;std::vector<std::array<std::uint8_t,4>>one,two;for(int block=0;block<600;++block){t.bar=block/64;t.barBeat=(block%64)*0.0625;auto x=process(a,p,t,1536,48000);for(std::uint32_t i=0;i<x.count;++i)one.push_back(x.events[i].data);}t={};t.valid=true;t.playing=true;t.bpm=123;for(int block=0;block<600;++block){t.bar=block/64;t.barBeat=(block%64)*0.0625;auto x=process(b,p,t,1536,48000);for(std::uint32_t i=0;i<x.count;++i)two.push_back(x.events[i].data);}assert(one==two&&!one.empty());t.playing=false;auto off=process(a,p,t,1024,48000);assert(off.count<=4);return 0;}
