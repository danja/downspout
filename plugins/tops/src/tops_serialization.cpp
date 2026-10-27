#include "tops_serialization.hpp"
#include <sstream>
namespace tops {
std::string serialize(const Params& p){ std::ostringstream ss; ss<<p.instrument<<" "<<p.engine<<" "<<p.preset<<" "<<p.pitch<<" "<<p.decay<<" "<<p.tone<<" "<<p.noise<<" "<<p.brightness<<" "<<p.volume<<" "<<p.distortion; return ss.str(); }
bool deserialize(const std::string& s, Params& p){ std::istringstream ss(s); ss>>p.instrument>>p.engine>>p.preset>>p.pitch>>p.decay>>p.tone>>p.noise>>p.brightness>>p.volume>>p.distortion; return !ss.fail(); }
}