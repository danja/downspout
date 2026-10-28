#pragma once

#include <cstdint>

namespace thumper {

enum class Engine {
    VirtualAnalog = 0,
    Modal = 1,
    Subtractive = 2,
    FM = 3,
    PhaseDistortion = 4,
    Wavetable = 5,
    EightOhEight = 6,
    NineOhNine = 7,
    Physical = 8,
    NoiseBurst = 9,
};

enum class NoteMap { Selected=0, GeneralMidi=1 };  // GeneralMidi: only notes 35 and 36 play
enum class Preset {
    Techno = 0,
    Gabber = 1,
    Rock = 2,
    Latin = 3,
    House = 4,
    Trap = 5,
    Acoustic = 6,
    Custom = 7,
};

struct Params {
    int engine = (int)Engine::VirtualAnalog;
    int engine2 = -1; // -1 = no layer
    int noteMap = 1;  // NoteMap
    int preset = (int)Preset::Techno;
    float pitch = 55.0f;       // Hz base
    float pitchDrop = 12.0f;   // semitones drop
    float decay = 0.5f;        // seconds
    float click = 0.5f;        // 0..1
    float body = 0.7f;         // 0..1
    float drive = 0.3f;        // 0..1
    float distortion = 0.0f;   // output distortion 0..1
    float tone = 0.5f;         // 0..1
    float volume = 0.8f;       // 0..1
    float fmRatio = 2.0f;
    float fmIndex = 3.0f;
    float modalDecay = 0.4f;
    float subMix = 0.5f;
    float layerMix = 0.0f;
    float pitchEnvDecay = 0.1f;
    float filterEnvDecay = 0.3f;
    float ampEnvDecay = 0.5f;
};

} // namespace thumper