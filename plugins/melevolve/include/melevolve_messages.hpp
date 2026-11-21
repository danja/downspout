#pragma once

#include <string>

namespace downspout::melevolve::messages {

// State keys shared by the DSP wrapper and the editor. "session" is the saved patch;
// "action" is a one-way editor-to-processor command and is never stored by the host.
inline constexpr const char* kSession = "session";
inline constexpr const char* kAction = "action";

inline std::string play(int position) { return "play:" + std::to_string(position); }
inline std::string favorite(int position) { return "fave:" + std::to_string(position); }
inline std::string vote(int position, int rating)
{
    return "vote:" + std::to_string(position) + ":" + std::to_string(rating);
}
inline std::string more() { return "more"; }
inline std::string evolve() { return "evolve"; }
inline std::string newPopulation() { return "new"; }
inline std::string resetAll() { return "reset"; }
inline std::string stop() { return "stop"; }

}  // namespace downspout::melevolve::messages
