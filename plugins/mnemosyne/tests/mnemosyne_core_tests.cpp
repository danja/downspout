#include "mnemosyne_core.hpp"
#include <cassert>
using namespace downspout::mnemosyne;
int main(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;Transport t;t.valid=true;t.playing=true;State a,b;resetRuntime(a);resetRuntime(b);auto x=process(a,p,t,24000,48000,nullptr,0);auto y=process(b,p,t,24000,48000,nullptr,0);assert(x.count==y.count&&x.count>0);for(std::uint32_t i=0;i<x.count;++i)assert(x.events[i].data==y.events[i].data);
MidiEvent note{};note.size=3;note.data={0x90,65,100,0};(void)process(a,p,t,1024,48000,&note,1);assert(a.capture.count==1);for(int i=0;i<100;++i){a.capture.count=kEventCapacity;(void)process(a,p,t,1024,48000,&note,1);assert(a.capture.count<=kEventCapacity);}
a.reservoir[0]=a.capture;a.phraseCount=1;auto text=serializeReservoir(a);State restored;assert(deserializeReservoir(text,restored));assert(restored.phraseCount==1);t.playing=false;auto off=process(a,p,t,1024,48000,nullptr,0);assert(off.count<=32);return 0;}
