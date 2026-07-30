#include "oracle_core.hpp"
#include <cassert>
#include <limits>
using namespace downspout::oracle;
int main(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;Transport t;t.valid=true;t.playing=true;std::array<float,1024>z{};State s;reset(s);auto silence=process(s,p,t,z.size(),48000,z.data(),z.data(),nullptr,0);assert(silence.count==4&&s.level==0);for(std::size_t i=0;i<z.size();++i)z[i]=(i%2?1.0f:-1.0f)*0.4f;auto noise=process(s,p,t,z.size(),48000,z.data(),z.data(),nullptr,0);assert(noise.count>=4&&std::isfinite(s.brightness));z[0]=std::numeric_limits<float>::infinity();(void)process(s,p,t,z.size(),48000,z.data(),z.data(),nullptr,0);assert(s.faults>0);p[kResponseChance]=1;p[kOnsetThreshold]=0.001f;t.barBeat=4;auto response=process(s,p,t,z.size(),48000,z.data(),z.data(),nullptr,0);assert(response.count<=6);return 0;}
