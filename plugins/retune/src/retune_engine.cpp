#include "retune_engine.hpp"

#include <algorithm>

namespace downspout::retune {

void Engine::setScale(const Scale& scale)
{
    setScale(std::make_shared<const Scale>(scale.cents.empty() ? equalTemperament() : scale));
}

void Engine::setScale(std::shared_ptr<const Scale> scale)
{
    if (scale && !scale->cents.empty()) scale_ = std::move(scale);
}

void Engine::setSettings(const Settings& settings)
{
    settings_ = settings;
    settings_.firstChannel = std::clamp(settings_.firstChannel, 0, 15);
    settings_.lastChannel = std::clamp(settings_.lastChannel, settings_.firstChannel, 15);
    settings_.rootNote = std::clamp(settings_.rootNote, 0, 127);
    if (settings_.bendRange <= 0.0) settings_.bendRange = 2.0;
}

// A free channel if any (the longest-idle one, so a release tail on a recently
// freed channel is least likely to be bent), else steal the oldest active note.
int Engine::pickChannel(std::vector<MidiOut>& out)
{
    int best = -1;
    for (int ch = settings_.firstChannel; ch <= settings_.lastChannel; ++ch) {
        const Slot& s = slots_[ch];
        if (!s.active && (best < 0 || s.order < slots_[best].order)) best = ch;
    }
    if (best >= 0) return best;

    best = settings_.firstChannel;
    for (int ch = settings_.firstChannel; ch <= settings_.lastChannel; ++ch)
        if (slots_[ch].order < slots_[best].order) best = ch;
    Slot& victim = slots_[best];
    out.push_back({static_cast<std::uint8_t>(0x80 | best), static_cast<std::uint8_t>(victim.carrier), 0});
    victim.active = false;
    return best;
}

void Engine::noteOn(int inChannel, int note, int velocity, std::vector<MidiOut>& out)
{
    if (velocity <= 0) {
        noteOff(inChannel, note, out);
        return;
    }
    // A repeated note-on for a held note retriggers: release the old one first.
    noteOff(inChannel, note, out);

    const auto tuning = tuneNote(*scale_, settings_.rootNote, note, settings_.bendRange);
    if (!tuning) return;

    const int ch = pickChannel(out);
    Slot& s = slots_[ch];
    s.active = true;
    s.inChannel = inChannel;
    s.inNote = note;
    s.carrier = tuning->carrier;
    s.order = ++clock_;
    out.push_back({static_cast<std::uint8_t>(0xE0 | ch), static_cast<std::uint8_t>(tuning->bend & 0x7F),
                   static_cast<std::uint8_t>((tuning->bend >> 7) & 0x7F)});
    out.push_back({static_cast<std::uint8_t>(0x90 | ch), static_cast<std::uint8_t>(tuning->carrier),
                   static_cast<std::uint8_t>(std::clamp(velocity, 1, 127))});
}

void Engine::noteOff(int inChannel, int note, std::vector<MidiOut>& out)
{
    for (int ch = settings_.firstChannel; ch <= settings_.lastChannel; ++ch) {
        Slot& s = slots_[ch];
        if (s.active && s.inChannel == inChannel && s.inNote == note) {
            out.push_back({static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(s.carrier), 0});
            s.active = false;
            s.order = ++clock_;
            return;
        }
    }
}

void Engine::allNotesOff(std::vector<MidiOut>& out)
{
    for (int ch = 0; ch < 16; ++ch) {
        Slot& s = slots_[ch];
        if (!s.active) continue;
        out.push_back({static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(s.carrier), 0});
        s.active = false;
        s.order = ++clock_;
    }
}

void Engine::other(const int status, const int data1, const int data2, std::vector<MidiOut>& out)
{
    const int type = (status >> 4) & 0xF;
    const int inChannel = status & 0xF;
    const auto d1 = static_cast<std::uint8_t>(data1 & 0x7F);
    const auto d2 = static_cast<std::uint8_t>(data2 & 0x7F);
    if (type == 0xB && (data1 == 120 || data1 == 123)) {
        allNotesOff(out);
        return;
    }
    if (type == 0xB || type == 0xC) {
        for (int ch = settings_.firstChannel; ch <= settings_.lastChannel; ++ch)
            out.push_back({static_cast<std::uint8_t>((type << 4) | ch), d1, d2});
    } else if (type == 0xA) {
        for (int ch = settings_.firstChannel; ch <= settings_.lastChannel; ++ch) {
            const Slot& s = slots_[ch];
            if (s.active && s.inChannel == inChannel && s.inNote == data1) {
                out.push_back({static_cast<std::uint8_t>(0xA0 | ch), static_cast<std::uint8_t>(s.carrier), d2});
                return;
            }
        }
    }
}

int Engine::activeNotes() const
{
    int n = 0;
    for (const Slot& s : slots_) n += s.active ? 1 : 0;
    return n;
}

}  // namespace downspout::retune
