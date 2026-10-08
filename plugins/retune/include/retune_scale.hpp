#pragma once

#include <optional>
#include <string>
#include <vector>

namespace downspout::retune {

// A Scala (.scl) scale: the cents of degrees 1..N above the 1/1 unison. The last
// entry is the period (the octave in most scales).
struct Scale {
    std::string description;
    std::vector<double> cents;

    [[nodiscard]] int size() const { return static_cast<int>(cents.size()); }
    [[nodiscard]] double period() const { return cents.empty() ? 1200.0 : cents.back(); }
};

// Strict .scl parser: '!' comment lines are skipped, then a description line, a
// note count, and exactly that many pitch lines. A pitch with a '.' is cents; one
// without is a ratio (a/b, or an integer a meaning a/1). Text after the first
// whitespace on a pitch line is ignored. Returns nullopt for anything malformed,
// including a count outside 1..1024, a non-positive ratio, or a non-positive period.
std::optional<Scale> parseScl(const std::string& text);

// Reads and parses a .scl file. Returns nullopt if it cannot be read, is larger
// than 256 KiB, or does not parse. Not for the audio thread.
std::optional<Scale> loadSclFile(const std::string& path);

// The 12-tone equal temperament scale, used as the default.
Scale equalTemperament();

// Pitch of `note` relative to `rootNote` mapped through the scale, in cents above
// the root. The scale repeats every period, and degree 0 is the root.
double noteCents(const Scale& scale, int rootNote, int note);

struct NoteTuning {
    int carrier = 60;  // the MIDI note number actually sounded
    int bend = 8192;   // 14-bit pitch bend that lands the carrier on the target
};

// Picks the nearest 12-TET carrier note and the bend that corrects it. Returns
// nullopt if the carrier would fall outside 0..127 or the needed bend exceeds the
// bend range. `bendRangeSemitones` must match the receiving synth (default 2).
std::optional<NoteTuning> tuneNote(const Scale& scale, int rootNote, int note, double bendRangeSemitones);

}  // namespace downspout::retune
