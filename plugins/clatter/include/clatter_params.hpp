#pragma once

namespace clatter {

// Synthesis families. Every sound belongs to one; the panel trims a whole family at a time.
enum class Family { Membrane = 0, Metal, Wood, Shaker, Clap };
constexpr int kFamilyCount = 5;
constexpr int kSoundCount = 31;

enum class NoteMap { Selected = 0, GeneralMidi = 1 };

struct FamilyTrim {
    float tune = 0.0f;   // semitones, -12..12
    float decay = 1.0f;  // length multiplier, 0.25..4
    float tone = 0.5f;   // 0..1, neutral at 0.5; meaning depends on the family (see docs/design.md)
};

struct Params {
    int sound = 0;                           // the sound played in NoteMap::Selected mode
    int noteMap = (int)NoteMap::GeneralMidi;
    FamilyTrim family[kFamilyCount];
    float level = 0.8f;                      // 0..1
    float width = 0.6f;                      // 0..1; stereo spread of each sound's fixed pan (0.6 = as designed)
};

}  // namespace clatter
