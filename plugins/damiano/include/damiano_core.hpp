#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::damiano {

inline constexpr std::uint32_t kMaxChannels = 8;

enum Mode : int {
    kModeSoft      = 0,
    kModeTanh      = 1,
    kModeFuzz      = 2,
    kModeOverdrive  = 3,
    kModeTube      = 4,
    kModeWavefold  = 5,
    kModeCount     = 6
};

struct Parameters {
    float mode       = static_cast<float>(kModeTanh);
    float drive      = 2.0f;    // 1.0–10.0
    float tone       = 50.0f;   // 0–100 (50 = flat)
    float foldCount  = 2.0f;    // 1–8, wavefold iterations
    float mix        = 100.0f;  // 0–100 dry/wet
    float outputGain = 0.0f;    // –24 to +24 dB
    float ccDrive    = 0.0f;    // 0–127 CC number (0 = disabled)
    float ccChannel  = 1.0f;    // 1–16

    // Stereo split (appended; defaults leave the plugin exactly as it was).
    // Linked (0): the right channel mirrors the left settings above.
    // Split (1): the right channel uses the R settings below, so the two ears
    // can be distorted differently (binaural effect).
    float stereo     = 0.0f;    // 0 = linked, 1 = split
    float driveR     = 2.0f;    // 1.0–10.0
    float modeR      = static_cast<float>(kModeTanh);
    float toneR      = 50.0f;   // 0–100
    float foldCountR = 2.0f;    // 1–8
    // Extra CC numbers (0 = disabled), all read on ccChannel. "Shape" is the
    // waveshaper mode: CC 0–127 is divided into kModeCount equal steps.
    float ccDriveR   = 0.0f;    // 0–127, right-channel drive (Split only)
    float ccShape    = 0.0f;    // 0–127, left (or both, when Linked) mode
    float ccShapeR   = 0.0f;    // 0–127, right-channel mode (Split only)
};

// Live values from MIDI CC; a negative number means "use the stored parameter".
struct LiveControl {
    float driveL = -1.0f;
    float driveR = -1.0f;
    float modeL  = -1.0f;
    float modeR  = -1.0f;
};

// CC value (0–127) to drive 1–10 and to a mode index 0–kModeCount-1.
[[nodiscard]] float driveFromCc(int value) noexcept;
[[nodiscard]] float modeFromCc(int value) noexcept;

struct EngineState {
    // One-pole low-pass state per channel for tone shelf
    std::array<float, kMaxChannels> toneLp {};
};

struct AudioBlock {
    std::array<const float*, kMaxChannels> inputs {};
    std::array<float*, kMaxChannels>       outputs {};
    std::uint32_t channelCount = 2;
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

void processBlock(EngineState&      state,
                  const Parameters& params,
                  std::uint32_t     frames,
                  double            sampleRate,
                  const AudioBlock& audio,
                  float             effectiveDrive) noexcept;

// As above with live CC values for both channels. `effectiveDrive` of the short
// form is `live.driveL` here; with Parameters::stereo == 0 the right-channel
// values in `live` and `params` are ignored.
void processBlock(EngineState&       state,
                  const Parameters&  params,
                  std::uint32_t      frames,
                  double             sampleRate,
                  const AudioBlock&  audio,
                  const LiveControl& live) noexcept;

[[nodiscard]] std::string          serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::damiano
