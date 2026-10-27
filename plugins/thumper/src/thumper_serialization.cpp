#include "thumper_serialization.hpp"
#include <sstream>

namespace thumper {
std::string serialize(const Params& p) {
    std::ostringstream ss;
    ss << p.engine << " " << p.preset << " " << p.pitch << " " << p.pitchDrop << " " << p.decay << " "
       << p.click << " " << p.body << " " << p.drive << " " << p.tone << " " << p.volume << " "
       << p.fmRatio << " " << p.fmIndex << " " << p.modalDecay << " " << p.subMix << " " << p.distortion << " "
       << p.pitchEnvDecay << " " << p.filterEnvDecay << " " << p.ampEnvDecay << " "
       << p.engine2 << " " << p.layerMix;
    return ss.str();
}
bool deserialize(const std::string& s, Params& p) {
    std::istringstream ss(s);
    ss >> p.engine >> p.preset >> p.pitch >> p.pitchDrop >> p.decay >> p.click >> p.body >> p.drive >> p.tone >> p.volume >> p.fmRatio >> p.fmIndex >> p.modalDecay >> p.subMix >> p.distortion >> p.pitchEnvDecay >> p.filterEnvDecay >> p.ampEnvDecay >> p.engine2 >> p.layerMix;
    return !ss.fail();
}
} // namespace thumper