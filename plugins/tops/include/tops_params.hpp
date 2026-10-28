#pragma once
#include <cstdint>
namespace tops {
enum class Instrument { ClosedHH=0, OpenHH=1, PedalHH=2, Crash=3, Ride=4, Splash=5, China=6, Cowbell=7, Tambourine=8, Triangle=9 };
enum class Engine { Metallic=0, Noise=1, FM=2, Physical=3, Wavetable=4, RingMod=5 };
enum class NoteMap { Selected=0, GeneralMidi=1 };
enum class Preset { Rock=0, Jazz=1, Electronic=2, Metal=3, Latin=4 };
struct Params {
    int instrument = (int)Instrument::ClosedHH;
    int engine = (int)Engine::Metallic;
    int preset = (int)Preset::Rock;
    int noteMap = (int)NoteMap::GeneralMidi;  // which notes play which instrument
    float pitch = 8000.0f; // Hz-ish
    float decay = 0.08f;
    float tone = 0.6f;
    float noise = 0.5f;
    float brightness = 0.7f;
    float volume = 0.7f;
    float distortion = 0.0f;
};
} // namespace tops