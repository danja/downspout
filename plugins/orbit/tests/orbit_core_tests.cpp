#include "orbit_core.hpp"
#include <cassert>
#include <cmath>
using namespace downspout::orbit;
int main(){std::array<float,kParameterCount>p{};for(std::size_t i=0;i<p.size();++i)p[i]=kParameterSpecs[i].defaultValue;Transport t;t.valid=true;t.playing=true;t.bpm=120;std::array<float,4096>in{},a{},b{},c{},d{};for(std::size_t i=0;i<in.size();++i)in[i]=std::sin(i*0.03f)*0.4f;State x,y;prepare(x,48000);prepare(y,48000);process(x,p,t,in.size(),48000,in.data(),in.data(),a.data(),b.data());process(y,p,t,in.size(),48000,in.data(),in.data(),c.data(),d.data());assert(a==c&&b==d);for(float v:a)assert(std::isfinite(v)&&std::fabs(v)<=1.5f);in.fill(0);reset(x);process(x,p,t,in.size(),48000,in.data(),in.data(),a.data(),b.data());for(float v:a)assert(v==0);return 0;}
