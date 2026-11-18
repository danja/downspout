#pragma once

#include "downspout/conductor_protocol.hpp"

// DrumGen's interpretation of Conductor Scene CC 20. Only the generic block
// decoder lives in the shared protocol; the musical response is local to this
// plugin so it can be changed without affecting other receivers.
namespace downspout::drumgen::conductor_scene {

inline constexpr bool kDefaultEnabled = false;

struct SceneResponse {
    bool apply = false;
    float fill = 0.3f;
};

[[nodiscard]] constexpr SceneResponse forSceneCc(const int value) noexcept
{
    using downspout::conductor_protocol::BlockType;
    using downspout::conductor_protocol::blockTypeFromSceneCc;

    switch (blockTypeFromSceneCc(value)) {
    case BlockType::Null:
        return {};
    case BlockType::Intro:
        return {true, 0.10f};
    case BlockType::Theme:
        return {true, 0.30f};
    case BlockType::Develop:
        return {true, 0.65f};
    case BlockType::Break:
        return {true, 1.0f};
    case BlockType::Reprise:
        return {true, 0.50f};
    case BlockType::Coda:
        return {true, 0.20f};
    }
    return {};
}

} // namespace downspout::drumgen::conductor_scene
