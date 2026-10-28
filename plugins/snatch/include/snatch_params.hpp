#pragma once
#include <cstdint>
namespace snatch {
enum class Engine { ToneNoise=0, Metal=1, Brush=2, Electronic=3, Acoustic=4, SideStick=5 };
enum class NoteMap { Selected=0, GeneralMidi=1 };
enum class Preset { Rock=0, Jazz=1, Electronic=2, Latin=3, Brushed=4 };
struct Params {
    int engine = 0;
    int preset = 0;
    int noteMap = 1;  // NoteMap: which notes play which engine
    float pitch = 200.0f;
    float tone = 0.5f;
    float noise = 0.7f;
    float snap = 0.6f;
    float body = 0.6f;
    float decay = 0.25f;
    float volume = 0.8f;
    float distortion = 0.0f;
};
} // namespace snatch