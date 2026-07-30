#include "polymeter_core.hpp"
#include <cassert>
#include <vector>
using namespace downspout::polymeter;
int main(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;Transport t;t.valid=true;t.playing=true;t.bpm=123;State a,b;std::vector<std::array<std::uint8_t,4>>one,two;for(int block=0;block<600;++block){t.bar=block/64;t.barBeat=(block%64)*0.0625;auto x=process(a,p,t,1536,48000);for(std::uint32_t i=0;i<x.count;++i)one.push_back(x.events[i].data);}t={};t.valid=true;t.playing=true;t.bpm=123;for(int block=0;block<600;++block){t.bar=block/64;t.barBeat=(block%64)*0.0625;auto x=process(b,p,t,1536,48000);for(std::uint32_t i=0;i<x.count;++i)two.push_back(x.events[i].data);}assert(one==two&&!one.empty());t.playing=false;auto off=process(a,p,t,1024,48000);assert(off.count<=4);return 0;}
