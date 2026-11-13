#pragma once

#include "bore_params.hpp"

#include <array>
#include <cstdint>

namespace downspout::bore {

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint32_t size = 0;
    std::array<std::uint8_t, 3> data {};
};

class Engine {
public:
    Engine();

    void activate(double sampleRate) noexcept;
    void setParameter(std::uint32_t index, float value) noexcept;
    [[nodiscard]] float getParameter(std::uint32_t index) const noexcept;
    [[nodiscard]] int activeVoices() const noexcept;

    void process(float* left,
                 float* right,
                 std::uint32_t frames,
                 const MidiMessage* events,
                 std::uint32_t eventCount) noexcept;

private:
    struct Voice {
        std::array<float, kMaxDelaySamples> bore {};
        int note = -1;
        std::uint8_t channel = 0;
        bool held = false;
        float envelope = 0.0f;
        float velocity = 1.0f;
        float boreFilter = 0.0f;
        float breathFilter = 0.0f;
        float flutterFilter = 0.0f;
        float reedOpening = 0.48f;
        float previousFlow = 0.0f;
        float flowHighpass = 0.0f;
        bool flowInitialized = false;
        double lipPhase = 0.0;
        std::uint32_t writeIndex = 0;
        std::uint32_t samplesWritten = 0;
        std::uint32_t random = 1;
        std::uint64_t age = 0;
    };

    void handleMidi(const MidiMessage& message) noexcept;
    void noteOn(int note, int velocity, std::uint8_t channel) noexcept;
    void noteOff(int note, std::uint8_t channel) noexcept;
    void allNotesOff(std::uint8_t channel) noexcept;
    void allSoundOff(std::uint8_t channel) noexcept;
    [[nodiscard]] std::size_t allocateVoice(int note, std::uint8_t channel) noexcept;
    void renderSegment(float* left, float* right, std::uint32_t frames) noexcept;

    std::array<float, kParameterCount> parameters_ {};
    std::array<Voice, kVoiceCount> voices_ {};
    double sampleRate_ = 48000.0;
    std::uint64_t age_ = 0;
    float pressure_ = 0.58f;
    float embouchure_ = 1.0f;
    float bell_ = 0.68f;
    float breath_ = 0.12f;
    float level_ = 0.65f;
    float reedInstability_ = 0.18f;
    float registerCapture_ = 0.0f;
    float smoothCoefficient_ = 0.0f;
};

}  // namespace downspout::bore
