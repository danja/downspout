#pragma once

#include "energise_core.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::energise {

// Versioned plugin state. Global settings stay in parameters; this document
// carries only the captured/rendered PCM, event list, groove/editor selections
// and A/B side. PCM is base64-encoded little-endian float32, in 64 KiB chunks.
struct StateData {
    int version = 1;
    ProcessorState state = ProcessorState::Idle;
    CaptureBuffer capture {};
    RenderedBuffer rendered {};
    std::vector<DrumEvent> events {};
    GrooveMap groove {};
    RenderSettings settings {};
    int selected = -1;
    int eventPage = 0;
    bool showingRender = false;
};

inline constexpr int kStateVersion = 1;
inline constexpr const char* kStateKey = "energise_project";
inline constexpr std::size_t kBase64ChunkBytes = 65536;

[[nodiscard]] std::string serializeState(const StateData& data);
[[nodiscard]] std::optional<StateData> deserializeState(const std::string& text);

[[nodiscard]] std::string base64Encode(const std::uint8_t* bytes, const std::size_t size);
[[nodiscard]] bool base64Decode(const std::string& text, std::vector<std::uint8_t>& bytes);

}  // namespace downspout::energise
