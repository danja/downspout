#include "snatch_serialization.hpp"
#include <sstream>
namespace snatch {
std::string serialize(const Params& p){ std::ostringstream ss; ss<<p.engine<<" "<<p.preset<<" "<<p.pitch<<" "<<p.tone<<" "<<p.noise<<" "<<p.snap<<" "<<p.body<<" "<<p.decay<<" "<<p.volume<<" "<<p.distortion<<" "<<p.noteMap; return ss.str(); }
static float clampf(float x,float lo,float hi){ return x<lo?lo:(x>hi?hi:x); }
bool deserialize(const std::string& s, Params& p){
    std::istringstream ss(s); Params q;
    ss>>q.engine>>q.preset>>q.pitch>>q.tone>>q.noise>>q.snap>>q.body>>q.decay>>q.volume>>q.distortion;
    if(ss.fail()) return false;
    ss>>q.noteMap; if(ss.fail()) q.noteMap=1;  // older strings have no note map
    if(q.engine<0||q.engine>5||q.preset<0||q.preset>4||q.noteMap<0||q.noteMap>1) return false;
    q.pitch=clampf(q.pitch,100,400); q.tone=clampf(q.tone,0,1); q.noise=clampf(q.noise,0,1); q.snap=clampf(q.snap,0,1);
    q.body=clampf(q.body,0,1); q.decay=clampf(q.decay,0.05f,1); q.volume=clampf(q.volume,0,1); q.distortion=clampf(q.distortion,0,1);
    p=q; return true;
}
}