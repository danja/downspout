#pragma once

#include "syrinx_params.hpp"
#include "SyrinxVoice.hpp"
#include "syrinx_analyzer.hpp"
#include "modules/DCBlocker.hpp"
#include "modules/ReverbModule.hpp"
#include "modules/Distortion.hpp"

#include <array>
#include <cstdint>

namespace downspout::syrinx {

static constexpr int kMaxVoices = 8;

struct StereoFrame {
    float left  = 0.0f;
    float right = 0.0f;
};

class Engine {
public:
    explicit Engine(float sampleRate = 48000.0f);

    void setSampleRate(float sampleRate);

    [[nodiscard]] float getParameter(std::uint32_t index) const;
    void setParameter(std::uint32_t index, float value);

    // MIDI
    void handleMidi(const std::uint8_t* data, std::uint32_t size);
    void handleNoteOn(std::uint8_t note, std::uint8_t velocity, int channel = 0);
    // channel < 0 releases the note on every channel.
    void handleNoteOff(std::uint8_t note, int channel = -1);
    void handlePitchBend(int channel, int value14);
    void handleCC(std::uint8_t cc, std::uint8_t value);

    // Audio in feeds the analyser when Source is Audio or Both; MIDI-only ignores it.
    [[nodiscard]] StereoFrame processStereo(float inL = 0.0f, float inR = 0.0f);
    void reset();

    // Randomize the selected preset's parameters (UI action)
    void randomizeSelectedPreset(std::uint32_t seed);

private:
    // One MIDI note-on plays a phrase: a run of short syllables, each with its own contour.
    struct Phrase {
        bool active = false;
        std::uint8_t note = 0;
        float velocity = 0.0f;
        int channel = 0;
        int index = 0;
        int count = 0;
        int countdown = 0;
        PresetParams base {};
        Xorshift32 rng {1u};
    };

    void rebuildFx();
    [[nodiscard]] int allocateVoice();
    [[nodiscard]] std::uint32_t selectedPreset() const;
    void fireSyllable(Phrase& phrase);
    void updateAudioVoice(float inL, float inR, float& wet);

    float sampleRate_;
    std::array<float, kParameterCount> params_{};
    std::uint32_t noiseStream_ = 0;

    // Per-channel pitch bend (MPE-style one note per channel); range 2 semitones, RPN 0.
    std::array<float, 16> bendRatio_ {};
    std::array<float, 16> bendRange_ {};
    std::array<int, 16> bendValue_ {};
    std::array<std::uint8_t, 16> rpnMsb_ {};
    std::array<std::uint8_t, 16> rpnLsb_ {};
    void resetBend();

    std::array<SyrinxVoice, kMaxVoices> voices_;
    std::array<Phrase, kMaxVoices> phrases_ {};

    // Audio-driven voice
    InputAnalyzer analyzer_;
    SyrinxVoice   drivenVoice_;
    Xorshift32    audioRng_ {99u};
    bool  drivenRunning_ = false;
    bool  drivenDirty_ = false;
    bool  gateOpen_ = false;
    bool  pitchValid_ = false;
    float logHz_ = 10.0f;       // log2 of the pitch the bird voice follows
    float ampSm_ = 0.0f, toneSm_ = 1.0f, noiseSm_ = 0.0f;
    int   silentSamples_ = 0;
    int   statusCounter_ = 0;

    ReverbModule reverbL_, reverbR_;
    DCBlocker    dcL_, dcR_;
    Distortion   distL_, distR_;
};

} // namespace downspout::syrinx
