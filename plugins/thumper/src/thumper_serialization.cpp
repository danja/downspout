#include "thumper_serialization.hpp"
#include <sstream>

namespace thumper {
std::string serialize(const Params& p) {
    std::ostringstream ss;
    ss << p.engine << " " << p.preset << " " << p.pitch << " " << p.pitchDrop << " " << p.decay << " "
       << p.click << " " << p.body << " " << p.drive << " " << p.tone << " " << p.volume << " "
       << p.fmRatio << " " << p.fmIndex << " " << p.modalDecay << " " << p.subMix << " " << p.distortion << " "
       << p.pitchEnvDecay << " " << p.filterEnvDecay << " " << p.ampEnvDecay << " "
       << p.engine2 << " " << p.layerMix << " " << p.noteMap;
    return ss.str();
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
bool deserialize(const std::string& s, Params& p) {
    std::istringstream ss(s);
    Params q;
    ss >> q.engine >> q.preset >> q.pitch >> q.pitchDrop >> q.decay >> q.click >> q.body >> q.drive >> q.tone >> q.volume >> q.fmRatio >> q.fmIndex >> q.modalDecay >> q.subMix >> q.distortion >> q.pitchEnvDecay >> q.filterEnvDecay >> q.ampEnvDecay >> q.engine2 >> q.layerMix;
    if (ss.fail()) return false;
    ss >> q.noteMap;
    if (ss.fail()) q.noteMap = 1;  // older strings have no note map
    if (q.noteMap < 0 || q.noteMap > 1) return false;
    if (q.engine < 0 || q.engine > 9 || q.engine2 < -1 || q.engine2 > 9 || q.preset < 0 || q.preset > 7) return false;
    q.pitch = clampf(q.pitch, 30, 120); q.pitchDrop = clampf(q.pitchDrop, 0, 24); q.decay = clampf(q.decay, 0.05f, 2);
    q.click = clampf(q.click, 0, 1); q.body = clampf(q.body, 0, 1); q.drive = clampf(q.drive, 0, 1); q.tone = clampf(q.tone, 0, 1);
    q.volume = clampf(q.volume, 0, 1); q.fmRatio = clampf(q.fmRatio, 0.5f, 8); q.fmIndex = clampf(q.fmIndex, 0, 10);
    q.modalDecay = clampf(q.modalDecay, 0.05f, 2); q.subMix = clampf(q.subMix, 0, 1); q.distortion = clampf(q.distortion, 0, 1);
    q.pitchEnvDecay = clampf(q.pitchEnvDecay, 0.001f, 2); q.filterEnvDecay = clampf(q.filterEnvDecay, 0.001f, 2);
    q.ampEnvDecay = clampf(q.ampEnvDecay, 0.001f, 2); q.layerMix = clampf(q.layerMix, 0, 1);
    p = q;
    return true;
}
} // namespace thumper
