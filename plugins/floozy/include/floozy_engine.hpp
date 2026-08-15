#pragma once

#include "floozy_params.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace downspout::floozy {

struct StereoFrame {
    float left = 0.0f;
    float right = 0.0f;
};

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 4> data {};
};

class FloozyEngine {
public:
    static constexpr std::size_t kMaxVoices = 8;

    explicit FloozyEngine(float sampleRate = 44100.0f);
    ~FloozyEngine();

    void setSampleRate(float sampleRate);
    void reset();

    float getParameter(std::uint32_t index) const;
    void setParameter(std::uint32_t index, float value);
    float getParameter(ParamId id) const;
    void setParameter(ParamId id, float value);

    void noteOn(int midiNote, std::uint8_t velocity);
    void noteOff(int midiNote);
    void allNotesOff();
    void handleMidi(const std::uint8_t* data, std::uint32_t size);
    void handleMidi(const MidiMessage& message);

    StereoFrame processStereo(float extLeft = 0.0f, float extRight = 0.0f);
    void processBlock(float* left, float* right, std::uint32_t frames, const MidiMessage* midi, std::uint32_t midiCount,
                      const float* extLeft = nullptr, const float* extRight = nullptr);

    std::size_t activeVoiceCount() const;
    bool hasActiveVoices() const;
    float sampleRate() const { return sampleRate_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    float sampleRate_;
};

} // namespace downspout::floozy
