#include "drift_core.hpp"
#include <cassert>
using namespace downspout::drift;
int main(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;
Transport t;t.valid=true;t.playing=true;t.bpm=120;State a,b;auto x=process(a,p,t,1024,48000,0.4f);auto y=process(b,p,t,1024,48000,0.4f);
assert(x.count==y.count&&x.count<=4);for(std::uint32_t i=0;i<x.count;++i){assert(x.events[i].data==y.events[i].data);assert(x.events[i].data[2]<=127);}
t.barBeat=1;auto z=process(a,p,t,48000,48000,0.9f);assert(z.count<=4);t.playing=false;assert(process(a,p,t,1024,48000,0).count==0);return 0;}
