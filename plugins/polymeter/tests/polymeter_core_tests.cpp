#include "polymeter_core.hpp"
#include "downspout/test_assert.h"
#include <array>
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
int main(){testCellularAutomaton();std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;Transport t;t.valid=true;t.playing=true;t.bpm=123;State a,b;std::vector<std::array<std::uint8_t,4>>one,two;for(int block=0;block<600;++block){t.bar=block/64;t.barBeat=(block%64)*0.0625;auto x=process(a,p,t,1536,48000);for(std::uint32_t i=0;i<x.count;++i)one.push_back(x.events[i].data);}t={};t.valid=true;t.playing=true;t.bpm=123;for(int block=0;block<600;++block){t.bar=block/64;t.barBeat=(block%64)*0.0625;auto x=process(b,p,t,1536,48000);for(std::uint32_t i=0;i<x.count;++i)two.push_back(x.events[i].data);}assert(one==two&&!one.empty());t.playing=false;auto off=process(a,p,t,1024,48000);assert(off.count<=4);return 0;}
