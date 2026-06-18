#pragma once

#include "basilico_params.hpp"
#include "basilico_transport.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace downspout::basilico {

struct StereoFrame {
    float left = 0.0f;
    float right = 0.0f;
};

class BasilicoEngine {
public:
    explicit BasilicoEngine(float sampleRate = 44100.0f);
    ~BasilicoEngine();

    void setSampleRate(float sampleRate);
    void reset();
    void setTransport(const TransportSnapshot& transport);

    float getParameter(std::uint32_t index) const;
    void setParameter(std::uint32_t index, float value);
    float getParameter(ParamId id) const;
    void setParameter(ParamId id, float value);

    void noteOn(int midiNote, std::uint8_t velocity);
    void noteOff(int midiNote);
    void allNotesOff();
    void handleMidi(const std::uint8_t* data, std::uint32_t size);

    StereoFrame processStereo();

    bool active() const;
    int currentNote() const;
    float currentFrequency() const;
    float sampleRate() const { return sampleRate_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    float sampleRate_;
};

} // namespace downspout::basilico
