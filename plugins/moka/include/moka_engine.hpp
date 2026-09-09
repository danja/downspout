#pragma once

#include "moka_params.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace downspout::moka {

struct StereoFrame {
    float left = 0.0f;
    float right = 0.0f;
};

class MokaEngine {
public:
    static constexpr std::size_t kMaxVoices = downspout::moka::kMaxVoices;

    explicit MokaEngine(float sampleRate = 44100.0f);
    ~MokaEngine();

    void setSampleRate(float sampleRate);
    void reset();

    float getParameter(std::uint32_t index) const;
    void setParameter(std::uint32_t index, float value);
    float getParameter(ParamId id) const;
    void setParameter(ParamId id, float value);

    void applyPreset(std::size_t presetIndex);

    void noteOn(int midiNote, std::uint8_t velocity);
    void noteOff(int midiNote);
    void allNotesOff();
    void handleMidi(const std::uint8_t* data, std::uint32_t size);

    StereoFrame processStereo();

    std::size_t activeVoiceCount() const;
    std::size_t voiceCap() const;
    float sampleRate() const { return sampleRate_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    float sampleRate_;
};

} // namespace downspout::moka
