#pragma once

#include "drumgen_core_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace downspout::drumgen {

struct PatternTemplate {
    std::string name;
    std::string source;
    std::string timeSig;
    std::string description;
    PatternState pattern;
};

class TemplateLibrary {
public:
    void scan(const std::string& directory);

    int count() const { return static_cast<int>(templates_.size()); }
    const PatternTemplate* get(int index) const;
    const PatternTemplate* findByName(const std::string& name) const;

    static std::optional<PatternTemplate> load(const std::string& path);

private:
    void scanDirectory(const std::string& directory);

    std::vector<PatternTemplate> templates_;
};

[[nodiscard]] int midiNoteForLane(LaneId lane, KitMapId kitMap);
void applyKitNotes(PatternState& pattern, KitMapId kitMap);

}  // namespace downspout::drumgen
