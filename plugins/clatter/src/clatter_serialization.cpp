#include "clatter_serialization.hpp"

#include <sstream>

namespace clatter {

namespace {
float clampf(const float x, const float lo, const float hi) { return x < lo ? lo : (x > hi ? hi : x); }
}

std::string serialize(const Params& p)
{
    std::ostringstream ss;
    ss << p.sound << " " << p.noteMap;
    for (const FamilyTrim& f : p.family)
        ss << " " << f.tune << " " << f.decay << " " << f.tone;
    ss << " " << p.level << " " << p.width;
    for (const FamilyTrim& f : p.family)
        ss << " " << f.pan;
    return ss.str();
}

bool deserialize(const std::string& s, Params& p)
{
    std::istringstream ss(s);
    Params q;
    ss >> q.sound >> q.noteMap;
    for (FamilyTrim& f : q.family)
        ss >> f.tune >> f.decay >> f.tone;
    ss >> q.level >> q.width;
    if (ss.fail())
        return false;
    for (FamilyTrim& f : q.family) {
        ss >> f.pan;
        if (ss.fail()) {  // older strings have no pans: keep the designed placement
            for (FamilyTrim& g : q.family) g.pan = 0.0f;
            break;
        }
    }
    if (q.sound < 0 || q.sound >= kSoundCount || q.noteMap < 0 || q.noteMap > 1)
        return false;
    for (FamilyTrim& f : q.family) {
        f.tune = clampf(f.tune, -12.0f, 12.0f);
        f.decay = clampf(f.decay, 0.25f, 4.0f);
        f.tone = clampf(f.tone, 0.0f, 1.0f);
        f.pan = clampf(f.pan, -1.0f, 1.0f);
    }
    q.level = clampf(q.level, 0.0f, 1.0f);
    q.width = clampf(q.width, 0.0f, 1.0f);
    p = q;
    return true;
}

}  // namespace clatter
