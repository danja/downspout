#pragma once

#include "drumkit_params.hpp"
#include "modules/Bitcrusher.hpp"
#include "modules/DCBlocker.hpp"
#include "modules/Distortion.hpp"
#include "modules/ReverbModule.hpp"
#include "voices/BashVoice.hpp"
#include "voices/ClapVoice.hpp"
#include "voices/ClaveVoice.hpp"
#include "voices/CowbellVoice.hpp"
#include "voices/CrashVoice.hpp"
#include "voices/HiHatVoice.hpp"
#include "voices/KickVoice.hpp"
#include "voices/SnareVoice.hpp"
#include "voices/TomVoice.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace downspout::drumkit {

struct StereoFrame {
    float left = 0.0f;
    float right = 0.0f;
};

class Engine {
public:
    explicit Engine(float sampleRate = 48000.0f);

    void setSampleRate(float sampleRate);
    [[nodiscard]] float getParameter(std::uint32_t index) const;
    void setParameter(std::uint32_t index, float value);
    void setInstrumentMuted(InstrumentId instrument, bool muted);
    [[nodiscard]] bool isInstrumentMuted(InstrumentId instrument) const;

    void handleMidi(const std::uint8_t* data, std::uint32_t size);
    void handleNoteOn(std::uint8_t note, std::uint8_t velocity);
    [[nodiscard]] StereoFrame processStereo();
    void reset();

private:
    static constexpr float kPi = 3.14159265359f;

    static void addPanned(float sample, float pan, float& left, float& right);
    [[nodiscard]] float processInstrument(InstrumentId instrument);
    void resetInstrument(InstrumentId instrument);
    void rebuildVoices();
    void applyParameter(std::uint32_t index, float value);

    float sampleRate_;
    std::array<float, kParameterCount> parameters_ {};
    std::array<bool, kInstrumentCount> muted_ {};

    std::unique_ptr<KickVoice> kick_;
    std::unique_ptr<SnareVoice> snare_;
    std::unique_ptr<ClapVoice> clap_;
    std::unique_ptr<TomVoice> loTom_;
    std::unique_ptr<TomVoice> hiTom_;
    std::unique_ptr<HiHatVoice> closedHH_;
    std::unique_ptr<HiHatVoice> openHH_;
    std::unique_ptr<CrashVoice> crash_;
    std::unique_ptr<BashVoice> bash_;
    std::unique_ptr<CowbellVoice> cowbell_;
    std::unique_ptr<ClaveVoice> clave_;

    Bitcrusher bitcrusherL_;
    Bitcrusher bitcrusherR_;
    Distortion distortionL_;
    Distortion distortionR_;
    ReverbModule reverbL_;
    ReverbModule reverbR_;
    DCBlocker dcBlockerL_;
    DCBlocker dcBlockerR_;
};

} // namespace downspout::drumkit
