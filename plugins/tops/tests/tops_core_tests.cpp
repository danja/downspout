#include "downspout/test_assert.h"
#include "tops_core_types.hpp"
#include "tops_serialization.hpp"
#include <cmath>
#include <cstring>
int main(){
    tops::TopsEngine e; e.setSampleRate(44100);
    tops::Params p; p.instrument=(int)tops::Instrument::OpenHH; e.setParams(p); e.trigger();
    // choke test: trigger closed
    tops::Params p2=p; p2.instrument=(int)tops::Instrument::ClosedHH; e.setParams(p2); e.trigger();
    float L[256],R[256]; std::memset(L,0,sizeof(L)); std::memset(R,0,sizeof(R));
    e.render(L,R,256);
    float sum=0; for(int i=0;i<256;i++) sum+=std::fabs(L[i]);
    assert(sum>0.0f);
    std::string s=tops::serialize(p); tops::Params pp; assert(tops::deserialize(s,pp));
    return 0;
}