#include "snatch_serialization.hpp"
#include <sstream>
namespace snatch {
std::string serialize(const Params& p){ std::ostringstream ss; ss<<p.engine<<" "<<p.preset<<" "<<p.pitch<<" "<<p.tone<<" "<<p.noise<<" "<<p.snap<<" "<<p.body<<" "<<p.decay<<" "<<p.volume<<" "<<p.distortion; return ss.str(); }
bool deserialize(const std::string& s, Params& p){ std::istringstream ss(s); ss>>p.engine>>p.preset>>p.pitch>>p.tone>>p.noise>>p.snap>>p.body>>p.decay>>p.volume>>p.distortion; return !ss.fail(); }
}