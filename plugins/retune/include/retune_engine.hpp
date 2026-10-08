#pragma once

#include "retune_scale.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace downspout::retune {

struct MidiOut {
    std::uint8_t status = 0;  // includes the channel nibble
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

struct Settings {
    int rootNote = 60;
    double bendRange = 2.0;  // semitones, must match the receiving synth
    int firstChannel = 1;    // output channel pool, 0-based, inclusive
    int lastChannel = 15;
};

// Retunes note-on/off to a Scala scale with one pitch bend per sounding note.
// Because pitch bend is per channel, every held note gets its own output channel
// from a small pool (an MPE-style layout without a master channel). Input
// channels are ignored; all other messages are not handled here.
class Engine {
public:
    // The copying overload allocates; the shared_ptr one does not, so it is the one
    // to use from an audio thread.
    void setScale(const Scale& scale);
    void setScale(std::shared_ptr<const Scale> scale);
    void setSettings(const Settings& settings);

    // Appends the output messages to `out`. A note that cannot be tuned (carrier or
    // bend out of range) is dropped, and its later note-off is ignored.
    void noteOn(int inChannel, int note, int velocity, std::vector<MidiOut>& out);
    void noteOff(int inChannel, int note, std::vector<MidiOut>& out);
    void allNotesOff(std::vector<MidiOut>& out);

    // Everything that is not a note. Control change and program change are
    // broadcast to every pool channel (so sustain, mod wheel and volume reach all
    // notes); poly aftertouch follows its note to that note's channel. Channel
    // aftertouch, incoming pitch bend and system messages are dropped, because a
    // bend would undo the tuning. CC 120/123 also release every held note.
    void other(int status, int data1, int data2, std::vector<MidiOut>& out);

    [[nodiscard]] int activeNotes() const;

private:
    struct Slot {
        bool active = false;
        int inChannel = 0;
        int inNote = 0;
        int carrier = 0;
        std::uint64_t order = 0;  // when it started (active) or was released (free)
    };

    int pickChannel(std::vector<MidiOut>& out);

    std::shared_ptr<const Scale> scale_ = std::make_shared<const Scale>(equalTemperament());
    Settings settings_;
    std::array<Slot, 16> slots_ {};
    std::uint64_t clock_ = 0;
};

}  // namespace downspout::retune
