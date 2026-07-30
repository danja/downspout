#include "conductor_core.hpp"
#include <cassert>
using namespace downspout::conductor;
Transport at(double q,bool play=true){Transport t;t.valid=true;t.playing=play;t.bar=std::floor(q/4);t.barBeat=q-t.bar*4;return t;}
int main(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;
State a,b;auto one=process(a,p,at(0),1024,48000);auto two=process(b,p,at(0),1024,48000);assert(one.count==6&&two.count==6);
for(std::uint32_t i=0;i<one.count;++i)assert(one.events[i].data==two.events[i].data);
for(int bar=1;bar<256;++bar)(void)process(a,p,at(bar*4.0),1024,48000);
auto stop=process(a,p,at(1024,false),1024,48000);assert(stop.count<=1);
State seek;auto sought=process(seek,p,at(512),1024,48000);assert(sought.count==6);return 0;}
